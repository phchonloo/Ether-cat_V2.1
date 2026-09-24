#include "EthercatMotorControl.h"

#include "ecat_def.h"
#include "applInterface.h"
#include "cia402appl.h"

/* Motor fault and microstep settings are owned by the existing motor code. */
extern u8 ErrNumFlag;
extern s32 sxifen;
extern TCiA402Axis LocalAxes[MAX_AXES];

#define ECAT_MODE_BIT(mode)             (1UL << ((mode) - 1))
#define ECAT_SUPPORTED_DRIVE_MODES      \
    (ECAT_MODE_BIT(PROFILE_POSITION_MODE) | \
     ECAT_MODE_BIT(PROFILE_VELOCITY_MODE) | \
     ECAT_MODE_BIT(CYCLIC_SYNC_POSITION_MODE) | \
     ECAT_MODE_BIT(CYCLIC_SYNC_VELOCITY_MODE))

#define ECAT_CONTROLWORD_NEW_SETPOINT   0x0010U
#define ECAT_CONTROLWORD_RELATIVE       0x0040U
#define ECAT_CONTROLWORD_HALT           0x0100U
#define ECAT_STATUSWORD_MODE_BIT12      0x1000U
#define ECAT_POSITION_TOLERANCE         1L
#define ECAT_DEFAULT_MICROSTEPS_PER_REV 1000L

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

static s32 EthercatMotorControl_AbsRpmLimit(INT32 requested_limit)
{
    signed long long limit;

    limit = (signed long long)requested_limit;
    if (limit < 0)
    {
        limit = -limit;
    }

    if ((limit == 0) || (limit > ECAT_MOTOR_MAX_RPM))
    {
        return ECAT_MOTOR_MAX_RPM;
    }

    return (s32)limit;
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
         (signed long long)ECAT_MOTOR_DECEL_RPM_PER_SEC) /
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

    /*
     * Accept the standard bit-4 edge, and also accept a changed absolute
     * target while bit 4 is low.  The latter keeps PP usable with masters
     * that hold 0x6040 at 0x000F and only update 0x607A.  Relative moves
     * still require the CiA402 new-setpoint handshake so they cannot be
     * accumulated repeatedly.
     */
    if (((new_setpoint != 0U) &&
         (sProfilePositionSetpointSeen == 0U)) ||
        ((new_setpoint == 0U) &&
         ((axis->Objects.objControlWord &
           ECAT_CONTROLWORD_RELATIVE) == 0U) &&
         (axis->Objects.objTargetPosition !=
          sProfilePositionTarget)))
    {
        target = (signed long long)axis->Objects.objTargetPosition;
        if ((axis->Objects.objControlWord &
             ECAT_CONTROLWORD_RELATIVE) != 0U)
        {
            target += (signed long long)actual_position;
        }

        sProfilePositionTarget =
            EthercatMotorControl_ClampPosition(target);
        sProfilePositionSetpointSeen = 1U;
        axis->Objects.objStatusWord |= ECAT_STATUSWORD_MODE_BIT12;
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

    speed_limit = EthercatMotorControl_AbsRpmLimit(
        (INT32)axis->Objects.objProfileVelocity);

    return EthercatMotorControl_PositionCommand(
        sProfilePositionTarget,
        actual_position,
        speed_limit,
        target_reached,
        limited);
}

static s32 EthercatMotorControl_SelectCommand(TCiA402Axis *axis,
                                              INT16 mode,
                                              s32 actual_position,
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
        return EthercatMotorControl_PositionCommand(
            axis->Objects.objTargetPosition,
            actual_position,
            ECAT_MOTOR_MAX_RPM,
            target_reached,
            limited);

    case PROFILE_VELOCITY_MODE:
    case CYCLIC_SYNC_VELOCITY_MODE:
        raw_rpm = (s32)axis->Objects.objTargetVelocity;
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
    sProfilePositionSetpointSeen = 0U;

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
    axis->Objects.objProfileVelocity = (UINT32)ECAT_MOTOR_MAX_RPM;
    axis->Objects.objSupportedDriveModes = ECAT_SUPPORTED_DRIVE_MODES;
}

s32 EthercatMotorControl_ApplyRamp(s32 target_base_rpm)
{
    s32 maximum_base_rpm;
    s32 ramp_rate;
    s32 ramp_increment;

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
        ramp_rate = ECAT_MOTOR_DECEL_RPM_PER_SEC;
    }
    else
    {
        ramp_rate = ECAT_MOTOR_ACCEL_RPM_PER_SEC;
    }

    ramp_increment =
        ramp_rate * ECAT_MOTOR_BASE_UNITS_PER_RPM;
    sRampAccumulator += ramp_increment;

    if (sRampAccumulator >= ECAT_MOTOR_CONTROL_HZ)
    {
        sRampAccumulator -= ECAT_MOTOR_CONTROL_HZ;
        if (target_base_rpm > sAppliedBaseRpm)
        {
            sAppliedBaseRpm++;
            if (sAppliedBaseRpm > target_base_rpm)
            {
                sAppliedBaseRpm = target_base_rpm;
            }
        }
        else
        {
            sAppliedBaseRpm--;
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
    s32 actual_position;
    s32 target_rpm;
    u8 command_enabled;
    u8 target_reached;
    u8 limited;

    actual_position = sPositionActual;
    axis->Objects.objPositionActualValue = (INT32)actual_position;
    axis->Objects.objVelocityActualValue =
        (INT32)sVelocityActualRpm;
    axis->Objects.objSupportedDriveModes =
        ECAT_SUPPORTED_DRIVE_MODES;

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
        sProfilePositionTarget = actual_position;
        sProfilePositionSetpointSeen = 0U;
        sLastMode = requested_mode;
        return;
    }

    /* Convert the local hardware fault into a CiA402 fault transition once. */
    if (ErrNumFlag != 0U)
    {
        EthercatMotorControl_Disable();
        axis->Objects.objStatusWord &=
            (UINT16)~STATUSWORD_DRIVE_FOLLOWS_COMMAND;

        if ((axis->bAxisIsActive != FALSE) &&
            (axis->i16State != STATE_FAULT_REACTION_ACTIVE) &&
            (axis->i16State != STATE_FAULT))
        {
            CiA402_LocalError(ERROR_MOTOR_ERROR);
        }
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
