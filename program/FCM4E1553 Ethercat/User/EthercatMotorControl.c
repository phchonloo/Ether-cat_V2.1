#include "EthercatMotorControl.h"

#include "ecat_def.h"
#include "applInterface.h"
#include "cia402appl.h"
#include "Errorhadle.h"

/* Motor fault and microstep settings are owned by the existing motor code. */
extern s32 sxifen;
extern TCiA402Axis LocalAxes[MAX_AXES];

#define ECAT_MODE_BIT(mode)             (1UL << ((mode) - 1))
#define ECAT_SUPPORTED_DRIVE_MODES      \
    (ECAT_MODE_BIT(PROFILE_POSITION_MODE) | \
     ECAT_MODE_BIT(PROFILE_VELOCITY_MODE) | \
     ECAT_MODE_BIT(CYCLIC_SYNC_POSITION_MODE) | \
     ECAT_MODE_BIT(CYCLIC_SYNC_VELOCITY_MODE))

#define ECAT_CONTROLWORD_NEW_SETPOINT   0x0010U
#define ECAT_CONTROLWORD_CHANGE_IMMEDIATE 0x0020U
#define ECAT_CONTROLWORD_RELATIVE       0x0040U
#define ECAT_CONTROLWORD_HALT           0x0100U
#define ECAT_STATUSWORD_MODE_BIT12      0x1000U
#define ECAT_POSITION_TOLERANCE         1L
#define ECAT_DEFAULT_MICROSTEPS_PER_REV 1000L
#define ECAT_DEFAULT_SYNC_CYCLE_US      1000U
#define ECAT_CSP_FOLLOWING_TIME_MS      50L

typedef struct
{
    UINT32 sequence;
    UINT32 cycle_time_us;
    INT32 target_position;
    INT32 target_velocity;
    INT16 mode;
} TCyclicSetpoint;

volatile s32 gEcatBaseRpmCommand = 0;
volatile u8 gEcatMotorEnabled = 0U;

static volatile s32 sPositionActual = 0;
static volatile s32 sVelocityActualRpm = 0;
static volatile s32 sAppliedBaseRpm = 0;
static s32 sRampAccumulator = 0;
static signed long long sPositionRemainder = 0;
static INT16 sLastMode = NO_MODE;
static INT32 sProfilePositionTarget = 0;
static u8 sProfilePositionSetpointSeen = 0U;
static volatile TCyclicSetpoint sCyclicSetpoint;
static UINT32 sLastCspSequence = 0U;
static INT32 sPreviousCspTarget = 0;
static s32 sCspFeedForwardRpm = 0;

static s32 EthercatMotorControl_ClampRpm(s32 target_rpm)
{
    if (target_rpm > ECAT_MOTOR_MAX_RPM)
    {
        return ECAT_MOTOR_MAX_RPM;
    }

    if (target_rpm < -ECAT_MOTOR_MAX_RPM)
    {
        return -ECAT_MOTOR_MAX_RPM;
    }

    return target_rpm;
}

static s32 EthercatMotorControl_ProfileVelocityLimit(UINT32 requested_limit)
{
    if (requested_limit == 0U)
    {
        return ECAT_MOTOR_DEFAULT_PROFILE_RPM;
    }

    if (requested_limit > (UINT32)ECAT_MOTOR_MAX_RPM)
    {
        return ECAT_MOTOR_MAX_RPM;
    }

    return (s32)requested_limit;
}

static s32 EthercatMotorControl_RampLimit(UINT32 requested_rate,
                                          s32 default_rate)
{
    if (requested_rate == 0U)
    {
        return default_rate;
    }

    if (requested_rate > (UINT32)ECAT_MOTOR_MAX_RAMP_RPM_PER_SEC)
    {
        return ECAT_MOTOR_MAX_RAMP_RPM_PER_SEC;
    }

    return (s32)requested_rate;
}

static s32 EthercatMotorControl_IntegerSqrtLimit(
    signed long long value,
    s32 limit)
{
    s32 low = 0;
    s32 high = limit;
    s32 result = 0;

    while (low <= high)
    {
        s32 middle = low + ((high - low) >> 1);
        signed long long square =
            (signed long long)middle * (signed long long)middle;

        if (square <= value)
        {
            result = middle;
            low = middle + 1;
        }
        else
        {
            high = middle - 1;
        }
    }

    return result;
}

static u8 EthercatMotorControl_ModeSupported(INT16 mode)
{
    switch (mode)
    {
    case PROFILE_POSITION_MODE:
    case PROFILE_VELOCITY_MODE:
    case CYCLIC_SYNC_POSITION_MODE:
    case CYCLIC_SYNC_VELOCITY_MODE:
        return 1U;

    default:
        return 0U;
    }
}

static INT32 EthercatMotorControl_ClampPosition(signed long long position)
{
    if (position > 2147483647LL)
    {
        return (INT32)2147483647L;
    }

    if (position < -2147483647LL - 1LL)
    {
        return (INT32)(-2147483647L - 1L);
    }

    return (INT32)position;
}

static void EthercatMotorControl_Disable(void)
{
    /* Clear enable first so the 16 kHz ISR cannot consume a stale command. */
    gEcatMotorEnabled = 0U;
    gEcatBaseRpmCommand = 0;
    /* Re-entering OP must wait for a fresh cyclic setpoint. */
    sCyclicSetpoint.sequence = 0U;
    sLastCspSequence = 0U;
    sCspFeedForwardRpm = 0;
}

static void EthercatMotorControl_ResetProfilePosition(TCiA402Axis *axis,
                                                      INT32 actual_position)
{
    /* Do not resume an old PP move when the drive is enabled again.  A low
       bit 4 must first re-arm the next rising-edge set-point command. */
    sProfilePositionTarget = actual_position;
    sProfilePositionSetpointSeen = 1U;
    axis->Objects.objStatusWord &=
        (UINT16)~ECAT_STATUSWORD_MODE_BIT12;
}

static s32 EthercatMotorControl_PositionCommand(INT32 target_position,
                                                INT32 actual_position,
                                                s32 speed_limit,
                                                u8 *target_reached,
                                                u8 *limited)
{
    signed long long error;
    signed long long absolute_error;
    signed long long rpm;
    signed long long braking_speed_squared;
    s32 braking_rpm;
    s32 resolution;

    error = (signed long long)target_position -
            (signed long long)actual_position;
    absolute_error = (error < 0) ? -error : error;

    if (absolute_error <= ECAT_POSITION_TOLERANCE)
    {
        *target_reached = 1U;
        return 0;
    }

    *target_reached = 0U;
    resolution = sxifen;
    if (resolution <= 0)
    {
        resolution = ECAT_DEFAULT_MICROSTEPS_PER_REV;
    }

    /*
     * Use the configured profile velocity while there is enough distance
     * to run at it.  The braking-distance limiter below performs the
     * deceleration.  A proportional error-to-speed conversion made short
     * moves unnecessarily slow and could look as if PP/CSP did not start.
     */
    rpm = (signed long long)speed_limit;

    /*
     * Do not request a speed whose open-loop stopping distance is longer
     * than the remaining position error.  This keeps the velocity ramp from
     * carrying the estimated position through the target and oscillating.
     * d[steps] = rpm^2 * steps_per_rev / (120 * decel[rpm/s]).
     */
    braking_speed_squared =
        (absolute_error * 120LL *
         (signed long long)EthercatMotorControl_RampLimit(
             LocalAxes[0].Objects.objProfileDeceleration,
             ECAT_MOTOR_DECEL_RPM_PER_SEC)) /
        (signed long long)resolution;
    braking_rpm = EthercatMotorControl_IntegerSqrtLimit(
        braking_speed_squared,
        speed_limit);
    if (braking_rpm < 1)
    {
        braking_rpm = 1;
    }
    if (rpm > braking_rpm)
    {
        rpm = braking_rpm;
    }

    if (rpm > speed_limit)
    {
        rpm = speed_limit;
        *limited = 1U;
    }

    return (error < 0) ? -(s32)rpm : (s32)rpm;
}

static s32 EthercatMotorControl_ProfilePosition(TCiA402Axis *axis,
                                                s32 actual_position,
                                                u8 *target_reached,
                                                u8 *limited)
{
    u8 new_setpoint;
    signed long long target;
    s32 speed_limit;

    new_setpoint = ((axis->Objects.objControlWord &
                     ECAT_CONTROLWORD_NEW_SETPOINT) != 0U) ? 1U : 0U;

    /* PP single-set-point handshake: 0x002F arms bit 4 low, and the
       0x003F rising edge accepts 0x607A.  Changing 0x607A at 0x000F or
       0x002F must not start a move.  Bit 5 selects immediate execution. */
    if ((new_setpoint != 0U) &&
        (sProfilePositionSetpointSeen == 0U))
    {
        sProfilePositionSetpointSeen = 1U;
        if ((axis->Objects.objControlWord &
             ECAT_CONTROLWORD_CHANGE_IMMEDIATE) != 0U)
        {
            target = (signed long long)axis->Objects.objTargetPosition;
            if ((axis->Objects.objControlWord &
                 ECAT_CONTROLWORD_RELATIVE) != 0U)
            {
                target += (signed long long)actual_position;
            }

            sProfilePositionTarget =
                EthercatMotorControl_ClampPosition(target);
            axis->Objects.objStatusWord |= ECAT_STATUSWORD_MODE_BIT12;
        }
    }
    else if (new_setpoint == 0U)
    {
        sProfilePositionSetpointSeen = 0U;
        axis->Objects.objStatusWord &=
            (UINT16)~ECAT_STATUSWORD_MODE_BIT12;
    }

    if ((axis->Objects.objControlWord & ECAT_CONTROLWORD_HALT) != 0U)
    {
        *target_reached = 0U;
        return 0;
    }

    speed_limit = EthercatMotorControl_ProfileVelocityLimit(
        axis->Objects.objProfileVelocity);

    return EthercatMotorControl_PositionCommand(
        sProfilePositionTarget,
        actual_position,
        speed_limit,
        target_reached,
        limited);
}

static s32 EthercatMotorControl_CyclicPosition(TCiA402Axis *axis,
                                               INT32 target_position,
                                               s32 actual_position,
                                               UINT32 sequence,
                                               UINT32 cycle_time_us,
                                               u8 *target_reached,
                                               u8 *limited)
{
    signed long long error;
    signed long long correction;
    signed long long command;
    signed long long feed_forward;
    signed long long denominator;
    UINT32 elapsed_cycles;
    s32 speed_limit;
    s32 resolution;
    s32 stationary_command;

    speed_limit = EthercatMotorControl_ProfileVelocityLimit(
        axis->Objects.objProfileVelocity);
    resolution = sxifen;
    if (resolution <= 0)
    {
        resolution = ECAT_DEFAULT_MICROSTEPS_PER_REV;
    }

    if (sequence != sLastCspSequence)
    {
        elapsed_cycles = sequence - sLastCspSequence;
        if ((sLastCspSequence == 0U) || (elapsed_cycles > 100U))
        {
            /* Do not infer a velocity from an initial or stale target. */
            sCspFeedForwardRpm = 0;
        }
        else
        {
            if (cycle_time_us == 0U)
            {
                cycle_time_us = ECAT_DEFAULT_SYNC_CYCLE_US;
            }
            denominator = (signed long long)resolution *
                          (signed long long)cycle_time_us *
                          (signed long long)elapsed_cycles;
            feed_forward =
                ((signed long long)target_position -
                 (signed long long)sPreviousCspTarget) *
                60000000LL / denominator;
            if (feed_forward > speed_limit)
            {
                feed_forward = speed_limit;
                *limited = 1U;
            }
            else if (feed_forward < -speed_limit)
            {
                feed_forward = -speed_limit;
                *limited = 1U;
            }

            /* Smooth one-step PDO quantization at low speeds. */
            sCspFeedForwardRpm =
                (7 * sCspFeedForwardRpm + (s32)feed_forward) / 8;
        }
        sPreviousCspTarget = target_position;
        sLastCspSequence = sequence;
    }

    error = (signed long long)target_position -
            (signed long long)actual_position;
    *target_reached =
        ((error >= -ECAT_POSITION_TOLERANCE) &&
         (error <= ECAT_POSITION_TOLERANCE) &&
         (sCspFeedForwardRpm == 0) &&
         (sVelocityActualRpm == 0)) ? 1U : 0U;

    if (sCspFeedForwardRpm == 0)
    {
        /* Hold a stationary target with the existing braking limiter. */
        stationary_command = EthercatMotorControl_PositionCommand(
            target_position, actual_position, speed_limit,
            target_reached, limited);
        if (sVelocityActualRpm != 0)
        {
            *target_reached = 0U;
        }
        return stationary_command;
    }

    /* CSP follows the master's trajectory: velocity feed-forward plus
       a bounded position-error correction, both in rpm. */
    correction = error * 60000LL /
                 ((signed long long)resolution *
                  ECAT_CSP_FOLLOWING_TIME_MS);
    command = (signed long long)sCspFeedForwardRpm + correction;
    if (command > speed_limit)
    {
        command = speed_limit;
        *limited = 1U;
    }
    else if (command < -speed_limit)
    {
        command = -speed_limit;
        *limited = 1U;
    }

    return (s32)command;
}

static s32 EthercatMotorControl_SelectCommand(TCiA402Axis *axis,
                                              INT16 mode,
                                              s32 actual_position,
                                              INT32 cyclic_target_position,
                                              INT32 cyclic_target_velocity,
                                              UINT32 cyclic_sequence,
                                              UINT32 cyclic_cycle_time_us,
                                              u8 *target_reached,
                                              u8 *limited)
{
    s32 raw_rpm;
    s32 target_rpm;

    switch (mode)
    {
    case PROFILE_POSITION_MODE:
        return EthercatMotorControl_ProfilePosition(axis,
                                                    actual_position,
                                                    target_reached,
                                                    limited);

    case CYCLIC_SYNC_POSITION_MODE:
        axis->Objects.objStatusWord |= ECAT_STATUSWORD_MODE_BIT12;
        return EthercatMotorControl_CyclicPosition(
            axis, cyclic_target_position, actual_position,
            cyclic_sequence, cyclic_cycle_time_us,
            target_reached, limited);

    case PROFILE_VELOCITY_MODE:
    case CYCLIC_SYNC_VELOCITY_MODE:
        raw_rpm = (mode == CYCLIC_SYNC_VELOCITY_MODE) ?
                  (s32)cyclic_target_velocity :
                  (s32)axis->Objects.objTargetVelocity;
        target_rpm = EthercatMotorControl_ClampRpm(raw_rpm);
        if (target_rpm != raw_rpm)
        {
            *limited = 1U;
        }
        if (mode == CYCLIC_SYNC_VELOCITY_MODE)
        {
            axis->Objects.objStatusWord |= ECAT_STATUSWORD_MODE_BIT12;
        }
        *target_reached =
            ((sVelocityActualRpm >= (target_rpm - 1)) &&
             (sVelocityActualRpm <= (target_rpm + 1))) ? 1U : 0U;
        return target_rpm;

    default:
        return 0;
    }
}

void EthercatMotorControl_OnSyncCycle(void)
{
    TCiA402Axis *axis = &LocalAxes[0];

    /* Called by APPL_Application() on the selected SM/Sync0 cycle.
       Write the sequence last so the foreground sees one complete setpoint. */
    sCyclicSetpoint.mode = axis->Objects.objModesOfOperation;
    sCyclicSetpoint.target_position =
        axis->Objects.objTargetPosition;
    sCyclicSetpoint.target_velocity =
        axis->Objects.objTargetVelocity;
    sCyclicSetpoint.cycle_time_us = axis->u32CycleTime;
    sCyclicSetpoint.sequence++;
    if (sCyclicSetpoint.sequence == 0U)
    {
        sCyclicSetpoint.sequence = 1U;
    }
}

void EthercatMotorControl_Init(void)
{
    TCiA402Axis *axis = &LocalAxes[0];

    EthercatMotorControl_Disable();
    sPositionActual = 0;
    sVelocityActualRpm = 0;
    sAppliedBaseRpm = 0;
    sRampAccumulator = 0;
    sPositionRemainder = 0;
    sLastMode = NO_MODE;
    sProfilePositionTarget = 0;
    sProfilePositionSetpointSeen = 1U;
    sCyclicSetpoint.sequence = 0U;
    sCyclicSetpoint.cycle_time_us = ECAT_DEFAULT_SYNC_CYCLE_US;
    sCyclicSetpoint.target_position = 0;
    sCyclicSetpoint.target_velocity = 0;
    sCyclicSetpoint.mode = NO_MODE;
    sLastCspSequence = 0U;
    sPreviousCspTarget = 0;
    sCspFeedForwardRpm = 0;

    /* 0x1600/0x1A00 carry position, velocity and 0x6060 mode selection. */
    sRxPDOassign.u16SubIndex0 = 1U;
    sRxPDOassign.aEntries[0] = 0x1600U;
    sTxPDOassign.u16SubIndex0 = 1U;
    sTxPDOassign.aEntries[0] = 0x1A00U;

    sConfiguredModuleIdentList.u16SubIndex0 = 1U;
    sConfiguredModuleIdentList.aEntries[0] = CSV_CSP_MODULE_ID;
    sDetectedModuleIdentList.u16SubIndex0 = 1U;
    sDetectedModuleIdentList.aEntries[0] = CSV_CSP_MODULE_ID;

    /* Keep CSV as the power-up mode while allowing the master to change 0x6060. */
    axis->Objects.objModesOfOperation = CYCLIC_SYNC_VELOCITY_MODE;
    axis->Objects.objModesOfOperationDisplay = CYCLIC_SYNC_VELOCITY_MODE;
    axis->Objects.objProfileVelocity =
        (UINT32)ECAT_MOTOR_DEFAULT_PROFILE_RPM;
    axis->Objects.objProfileAcceleration =
        (UINT32)ECAT_MOTOR_ACCEL_RPM_PER_SEC;
    axis->Objects.objProfileDeceleration =
        (UINT32)ECAT_MOTOR_DECEL_RPM_PER_SEC;
    axis->Objects.objSupportedDriveModes = ECAT_SUPPORTED_DRIVE_MODES;
}

s32 EthercatMotorControl_ApplyRamp(s32 target_base_rpm)
{
    s32 maximum_base_rpm;
    s32 ramp_rate;
    s32 ramp_increment;
    s32 ramp_step;

    maximum_base_rpm =
        ECAT_MOTOR_MAX_RPM * ECAT_MOTOR_BASE_UNITS_PER_RPM;
    if (target_base_rpm > maximum_base_rpm)
    {
        target_base_rpm = maximum_base_rpm;
    }
    else if (target_base_rpm < -maximum_base_rpm)
    {
        target_base_rpm = -maximum_base_rpm;
    }

    if (target_base_rpm == sAppliedBaseRpm)
    {
        sRampAccumulator = 0;
        return sAppliedBaseRpm;
    }

    /*
     * Decelerate when reducing speed or reversing direction.  Acceleration
     * resumes only after a reversal has crossed zero.
     */
    if ((target_base_rpm == 0) ||
        ((target_base_rpm > 0) && (sAppliedBaseRpm < 0)) ||
        ((target_base_rpm < 0) && (sAppliedBaseRpm > 0)) ||
        ((target_base_rpm > 0) &&
         (target_base_rpm < sAppliedBaseRpm)) ||
        ((target_base_rpm < 0) &&
         (target_base_rpm > sAppliedBaseRpm)))
    {
        ramp_rate = EthercatMotorControl_RampLimit(
            LocalAxes[0].Objects.objProfileDeceleration,
            ECAT_MOTOR_DECEL_RPM_PER_SEC);
    }
    else
    {
        ramp_rate = EthercatMotorControl_RampLimit(
            LocalAxes[0].Objects.objProfileAcceleration,
            ECAT_MOTOR_ACCEL_RPM_PER_SEC);
    }

    ramp_increment =
        ramp_rate * ECAT_MOTOR_BASE_UNITS_PER_RPM;
    sRampAccumulator += ramp_increment;

    ramp_step = sRampAccumulator / ECAT_MOTOR_CONTROL_HZ;
    sRampAccumulator %= ECAT_MOTOR_CONTROL_HZ;

    if (ramp_step > 0)
    {
        if (target_base_rpm > sAppliedBaseRpm)
        {
            sAppliedBaseRpm += ramp_step;
            if (sAppliedBaseRpm > target_base_rpm)
            {
                sAppliedBaseRpm = target_base_rpm;
            }
        }
        else
        {
            sAppliedBaseRpm -= ramp_step;
            if (sAppliedBaseRpm < target_base_rpm)
            {
                sAppliedBaseRpm = target_base_rpm;
            }
        }
    }

    return sAppliedBaseRpm;
}

void EthercatMotorControl_EmergencyStop(void)
{
    sAppliedBaseRpm = 0;
    sRampAccumulator = 0;
}

void EthercatMotorControl_FastUpdate(s32 applied_base_rpm)
{
    signed long long denominator;
    s32 resolution;

    sVelocityActualRpm =
        applied_base_rpm / ECAT_MOTOR_BASE_UNITS_PER_RPM;

    /* A stopped motor cannot advance position; avoid 64-bit work at 16 kHz. */
    if (applied_base_rpm == 0)
    {
        return;
    }

    resolution = sxifen;
    if (resolution <= 0)
    {
        resolution = ECAT_DEFAULT_MICROSTEPS_PER_REV;
    }

    denominator =
        (signed long long)ECAT_MOTOR_BASE_UNITS_PER_RPM *
        60LL * (signed long long)ECAT_MOTOR_CONTROL_HZ;
    sPositionRemainder +=
        (signed long long)applied_base_rpm *
        (signed long long)resolution;

    while (sPositionRemainder >= denominator)
    {
        sPositionRemainder -= denominator;
        sPositionActual++;
    }

    while (sPositionRemainder <= -denominator)
    {
        sPositionRemainder += denominator;
        sPositionActual--;
    }
}

void EthercatMotorControl_Update(void)
{
    TCiA402Axis *axis = &LocalAxes[0];
    INT16 requested_mode;
    INT16 cyclic_mode;
    INT32 cyclic_target_position;
    INT32 cyclic_target_velocity;
    UINT32 cyclic_sequence;
    UINT32 cyclic_cycle_time_us;
    s32 actual_position;
    s32 target_rpm;
    u8 command_enabled;
    u8 target_reached;
    u8 limited;
    u8 fault_code;
    UINT16 cia_error_code;

    actual_position = sPositionActual;
    axis->Objects.objPositionActualValue = (INT32)actual_position;
    axis->Objects.objVelocityActualValue =
        (INT32)sVelocityActualRpm;
    axis->Objects.objSupportedDriveModes =
        ECAT_SUPPORTED_DRIVE_MODES;

    /* Publish the latched detector result before any mode-dependent return. */
    fault_code = ErrNumFlag;
    if (fault_code != 0U)
    {
        EthercatMotorControl_Disable();
        EthercatMotorControl_ResetProfilePosition(axis,
                                                  actual_position);
        axis->Objects.objStatusWord &=
            (UINT16)~STATUSWORD_DRIVE_FOLLOWS_COMMAND;
        u16VendorErrorCode = fault_code;
        switch (fault_code)
        {
        case 11U: /* Motor phase disconnected: dedicated detector required. */
            cia_error_code = ERROR_ARMATURE_CIRCUIT_INTERRUPTED;
            u16ErrorRegister = 0x21U;
            break;
        case 13U: /* DC bus under-voltage. */
            cia_error_code = ERROR_DC_LINK_UNDER_VOLTAGE;
            u16ErrorRegister = 0x05U;
            break;
        case 14U: /* DC bus over-voltage. */
            cia_error_code = ERROR_DC_LINK_OVER_VOLTAGE;
            u16ErrorRegister = 0x05U;
            break;
        case 10U: /* VFO asserts a driver protection fault. */
        default:
            cia_error_code = ERROR_MOTOR_ERROR;
            u16ErrorRegister = 0x21U;
            break;
        }
        if ((axis->bAxisIsActive != FALSE) &&
            (axis->i16State != STATE_FAULT_REACTION_ACTIVE) &&
            (axis->i16State != STATE_FAULT))
        {
            CiA402_LocalError(cia_error_code);
        }
        return;
    }

    requested_mode = axis->Objects.objModesOfOperation;
    if (EthercatMotorControl_ModeSupported(requested_mode) != 0U)
    {
        axis->Objects.objModesOfOperationDisplay = requested_mode;
    }
    else
    {
        axis->Objects.objModesOfOperationDisplay = NO_MODE;
        EthercatMotorControl_Disable();
        axis->Objects.objStatusWord &=
            (UINT16)~(STATUSWORD_DRIVE_FOLLOWS_COMMAND |
                      STATUSWORD_TARGET_REACHED |
                      STATUSWORD_INTERNAL_LIMIT);
        sLastMode = NO_MODE;
        return;
    }

    /* Stop for one foreground cycle whenever the selected mode changes. */
    if (requested_mode != sLastMode)
    {
        EthercatMotorControl_Disable();
        axis->Objects.objStatusWord &=
            (UINT16)~(STATUSWORD_DRIVE_FOLLOWS_COMMAND |
                      STATUSWORD_TARGET_REACHED |
                      STATUSWORD_INTERNAL_LIMIT);
        EthercatMotorControl_ResetProfilePosition(axis,
                                                  actual_position);
        sLastCspSequence = 0U;
        sCspFeedForwardRpm = 0;
        sLastMode = requested_mode;
        return;
    }

    command_enabled = 0U;
    if (((nAlStatus & STATE_MASK) == STATE_OP) &&
        (axis->bAxisIsActive != FALSE) &&
        (axis->i16State == STATE_OPERATION_ENABLED))
    {
        command_enabled = 1U;
    }

    if (command_enabled == 0U)
    {
        EthercatMotorControl_Disable();
        EthercatMotorControl_ResetProfilePosition(axis,
                                                  actual_position);
        axis->Objects.objStatusWord &=
            (UINT16)~(STATUSWORD_DRIVE_FOLLOWS_COMMAND |
                      STATUSWORD_TARGET_REACHED |
                      STATUSWORD_INTERNAL_LIMIT);
        return;
    }

    /* A cyclic mode must use a complete setpoint from an application cycle,
       not an object that may be changing during PDO output mapping. */
    do
    {
        cyclic_sequence = sCyclicSetpoint.sequence;
        cyclic_mode = sCyclicSetpoint.mode;
        cyclic_target_position = sCyclicSetpoint.target_position;
        cyclic_target_velocity = sCyclicSetpoint.target_velocity;
        cyclic_cycle_time_us = sCyclicSetpoint.cycle_time_us;
    } while (cyclic_sequence != sCyclicSetpoint.sequence);

    if (((requested_mode == CYCLIC_SYNC_POSITION_MODE) ||
         (requested_mode == CYCLIC_SYNC_VELOCITY_MODE)) &&
        ((cyclic_sequence == 0U) || (cyclic_mode != requested_mode)))
    {
        EthercatMotorControl_Disable();
        axis->Objects.objStatusWord &=
            (UINT16)~(STATUSWORD_DRIVE_FOLLOWS_COMMAND |
                      STATUSWORD_TARGET_REACHED |
                      STATUSWORD_INTERNAL_LIMIT);
        return;
    }

    target_reached = 0U;
    limited = 0U;
    target_rpm = EthercatMotorControl_SelectCommand(axis,
                                                    requested_mode,
                                                    actual_position,
                                                    cyclic_target_position,
                                                    cyclic_target_velocity,
                                                    cyclic_sequence,
                                                    cyclic_cycle_time_us,
                                                    &target_reached,
                                                    &limited);

    if (target_reached != 0U)
    {
        axis->Objects.objStatusWord |= STATUSWORD_TARGET_REACHED;
    }
    else
    {
        axis->Objects.objStatusWord &=
            (UINT16)~STATUSWORD_TARGET_REACHED;
    }

    if (limited != 0U)
    {
        axis->Objects.objStatusWord |= STATUSWORD_INTERNAL_LIMIT;
    }
    else
    {
        axis->Objects.objStatusWord &=
            (UINT16)~STATUSWORD_INTERNAL_LIMIT;
    }

    /* Publish command first and enable last for an atomic ISR hand-off. */
    gEcatBaseRpmCommand =
        target_rpm * ECAT_MOTOR_BASE_UNITS_PER_RPM;
    gEcatMotorEnabled = 1U;
}
