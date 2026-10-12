#include "EthercatMotorControl.h"

#include "ecat_def.h"
#include "applInterface.h"
#include "cia402appl.h"
#include "Errorhadle.h"
#include "EcatPulsePosition.h"
#include "qspi.h"
#include "ecatappl.h"

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
#define ECAT_DEFAULT_MICROSTEPS_PER_REV 1000L
#define ECAT_DEFAULT_SYNC_CYCLE_US      1000U
/* A 20 ms phase follower smooths quantized 2 ms PDO positions.  It uses
 * phase units, not integer-rpm feed-forward or rounded position feedback. */
#define ECAT_CSP_FILTER_TICKS           (ECAT_MOTOR_CONTROL_HZ / 50L)

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
static s32 sCspStepFraction = 0;
static TEcatPulsePosition sPulsePosition;
static signed long long sForegroundPhase = 0;
static volatile UINT32 sFeedbackSequence = 0U;
static volatile signed long long sPhaseFeedback = 0;
static s32 sPhaseBefore = 0;
static s32 sPreparedPhaseStep = 0;
static u8 sPhasePrepared = 0U;
static signed long long sPreparedTargetPhase = 0;
static u8 sPreparedPositionMode = 0U;
volatile TEcatPulseDiagnostics gEcatPulseDiag = {0};

typedef struct
{
    signed long long target_phase;
    s32 base_rpm;
    INT32 target_position;
    INT16 mode;
    u8 position_mode;
    s32 speed_limit_base;
} TMotorPulseCommand;
/* Foreground writes the inactive slot; a single 32-bit publication selects it.
 * TIM8 is the highest-priority ISR, so it never waits for a preempted writer. */
static volatile TMotorPulseCommand sPulseCommand[2];
static volatile UINT32 sPulseCommandIndex = 0U;
static INT16 sLastMode = NO_MODE;
static INT32 sProfilePositionTarget = 0;
static u8 sProfilePositionSetpointSeen = 0U;
static volatile TCyclicSetpoint sCyclicSetpoint;

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
    signed long long phase_error;
    signed long long phase_distance;
    signed long long rpm;
    signed long long braking_speed_squared;
    s32 braking_rpm;
    s32 resolution;

    (void)actual_position;
    resolution = sPulsePosition.resolution;
    phase_error = EcatPulse_TargetPhase(target_position, resolution) -
                  sForegroundPhase;
    if (phase_error == 0)
    {
        *target_reached = 1U;
        return 0;
    }

    *target_reached = 0U;
    /* Remaining emitted-count budget, rounded UP for the speed planner.
     * The ISR, not this rpm calculation, clips the last excitation increment. */
    error = phase_error;
    phase_distance = (error < 0) ? -error : error;
    absolute_error = (phase_distance / ECAT_PHASE_UNITS_PER_REV) * resolution;
    absolute_error += ((phase_distance % ECAT_PHASE_UNITS_PER_REV) *
                      resolution + ECAT_PHASE_UNITS_PER_REV - 1) /
                      ECAT_PHASE_UNITS_PER_REV;

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
     * carrying the counted position through the target and oscillating.
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
    signed long long remaining;
    signed long long command;
    s32 speed_limit;
    (void)actual_position;
    (void)sequence;
    (void)cycle_time_us;
    speed_limit = EthercatMotorControl_ProfileVelocityLimit(
        axis->Objects.objProfileVelocity);
    remaining = EcatPulse_TargetPhase(target_position,
                                      sPulsePosition.resolution) -
                sForegroundPhase;
    *target_reached = ((remaining == 0) &&
                      (sVelocityActualRpm == 0)) ? 1U : 0U;
    /* Diagnostic rpm only. TIM8 calculates the full-resolution phase rate
     * from its own current phase, with fractional accumulation below 1 rpm.
     * No previous target/velocity is carried across a target reversal. */
    command = remaining / (ECAT_CSP_FILTER_TICKS *
                           ECAT_MOTOR_BASE_UNITS_PER_RPM);
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
    s32 resolution = sxifen;

    EthercatMotorControl_Disable();
    sPositionActual = 0;
    sVelocityActualRpm = 0;
    sAppliedBaseRpm = 0;
    sRampAccumulator = 0;
    sCspStepFraction = 0;
    memset((void *)&gEcatPulseDiag, 0, sizeof(gEcatPulseDiag));
    if ((resolution <= 0) || (resolution > ECAT_PULSE_MAX_RESOLUTION))
    {
        resolution = ECAT_DEFAULT_MICROSTEPS_PER_REV;
        gEcatPulseDiag.resolutionChanged = 1U;
    }
    EcatPulse_Reset(&sPulsePosition, resolution);
    gEcatPulseDiag.resolution = resolution;
    sPhaseFeedback = 0;
    sForegroundPhase = 0;
    sFeedbackSequence = 0U;
    sPhasePrepared = 0U;
    memset((void *)sPulseCommand, 0, sizeof(sPulseCommand));
    sPulseCommandIndex = 0U;
    sLastMode = NO_MODE;
    sProfilePositionTarget = 0;
    sProfilePositionSetpointSeen = 1U;
    sCyclicSetpoint.sequence = 0U;
    sCyclicSetpoint.cycle_time_us = ECAT_DEFAULT_SYNC_CYCLE_US;
    sCyclicSetpoint.target_position = 0;
    sCyclicSetpoint.target_velocity = 0;
    sCyclicSetpoint.mode = NO_MODE;

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
    sCspStepFraction = 0;
}

void EthercatMotorControl_BeginPhaseCycle(s32 angle)
{
    sPhaseBefore = angle;
    sPhasePrepared = 0U;
}

static s32 EthercatMotorControl_CspPhaseRate(
    signed long long remaining, s32 maximum_step)
{
    s32 numerator;
    s32 step;
    s32 threshold = maximum_step * ECAT_CSP_FILTER_TICKS;
    if (remaining > threshold)
    {
        sCspStepFraction = 0;
        return maximum_step;
    }
    if (remaining < -threshold)
    {
        sCspStepFraction = 0;
        return -maximum_step;
    }
    if (remaining == 0)
    {
        sCspStepFraction = 0;
        return 0;
    }
    if (((remaining > 0) && (sCspStepFraction < 0)) ||
        ((remaining < 0) && (sCspStepFraction > 0)))
        sCspStepFraction = 0;
    /* Once bounded above, the hot path needs only 32-bit division.
     * Retain the sub-unit remainder so a tiny error finishes exactly,
     * without a minimum-1-rpm kick at a stationary endpoint. */
    numerator = (s32)remaining + sCspStepFraction;
    step = numerator / ECAT_CSP_FILTER_TICKS;
    sCspStepFraction = numerator % ECAT_CSP_FILTER_TICKS;
    return step;
}

s32 EthercatMotorControl_NextPhaseStep(void)
{
    UINT32 index = sPulseCommandIndex;
    TMotorPulseCommand command;
    s32 step;
    command.target_phase = sPulseCommand[index].target_phase;
    command.base_rpm = sPulseCommand[index].base_rpm;
    command.target_position = sPulseCommand[index].target_position;
    command.mode = sPulseCommand[index].mode;
    command.position_mode = sPulseCommand[index].position_mode;
    command.speed_limit_base = sPulseCommand[index].speed_limit_base;

    if (gEscPdiDiag.faultLatched != 0U)
    {
        EthercatMotorControl_EmergencyStop();
        gEcatMotorEnabled = 0U;
        return 0;
    }
    if ((sxifen != sPulsePosition.resolution) ||
        (gEcatPulseDiag.resolutionChanged != 0U) ||
        (gEcatPulseDiag.positionLimit != 0U))
    {
        if (sxifen != sPulsePosition.resolution)
            gEcatPulseDiag.resolutionChanged = 1U;
        EthercatMotorControl_EmergencyStop();
        gEcatMotorEnabled = 0U;
        return 0;
    }
    if (command.mode == CYCLIC_SYNC_POSITION_MODE)
        step = EthercatMotorControl_CspPhaseRate(command.target_phase -
                    sPulsePosition.phase, command.speed_limit_base);
    else
        step = command.base_rpm;
    if ((command.mode == CYCLIC_SYNC_POSITION_MODE) &&
        (step >= -1) && (step <= 1) &&
        (sAppliedBaseRpm >= -1) && (sAppliedBaseRpm <= 1))
    {
        /* Preserve the phase follower's fractional 0/1-unit pulses.
         * Running each isolated unit through the integer acceleration ramp
         * would erase it forever. One unit is only 1/15360000 revolution. */
        sAppliedBaseRpm = step;
        sRampAccumulator = 0;
    }
    else
        step = EthercatMotorControl_ApplyRamp(step);
    if ((command.mode == CYCLIC_SYNC_POSITION_MODE) &&
        (((step > 0) && (command.target_phase < sPulsePosition.phase)) ||
         ((step < 0) && (command.target_phase > sPulsePosition.phase))))
    {
        /* Old ramp direction must not travel away from a new CSP target.
         * A commanded reversal resumes after the rate planner crosses zero. */
        step = 0;
        if (gEcatPulseDiag.blockedAwayCount < UINT32_MAX)
            gEcatPulseDiag.blockedAwayCount++;
    }
    step = EcatPulse_LimitStep(&sPulsePosition, command.target_phase,
                              step, command.position_mode);
    /* PP can stop its ramp at the endpoint. CSP keeps the rate planner alive
     * between successive targets; resetting it every PDO would cause stutter. */
    if ((command.position_mode != 0U) &&
        (command.target_phase == sPulsePosition.phase) &&
        (command.mode == PROFILE_POSITION_MODE))
        EthercatMotorControl_EmergencyStop();
    sPreparedPhaseStep = step;
    sPreparedTargetPhase = command.target_phase;
    sPreparedPositionMode = command.position_mode;
    sPhasePrepared = 1U;
    gEcatPulseDiag.targetPosition = command.target_position;
    gEcatPulseDiag.requestedPhaseStep = step;
    return step;
}

void EthercatMotorControl_EndPhaseCycle(s32 angle)
{
    s32 step = angle - sPhaseBefore;
    s32 step_delta;
    if (step > ECAT_PHASE_UNITS_PER_CYCLE / 2)
        step -= ECAT_PHASE_UNITS_PER_CYCLE;
    else if (step < -ECAT_PHASE_UNITS_PER_CYCLE / 2)
        step += ECAT_PHASE_UNITS_PER_CYCLE;
    if (sPhasePrepared == 0U)
    {
        if (gEcatPulseDiag.skippedCycles < UINT32_MAX)
            gEcatPulseDiag.skippedCycles++;
    }
    else
    {
        if (gEcatPulseDiag.countedCycles < UINT32_MAX)
            gEcatPulseDiag.countedCycles++;
        if ((step != sPreparedPhaseStep) &&
            (gEcatPulseDiag.mismatchCount < UINT32_MAX))
            gEcatPulseDiag.mismatchCount++;
    }
    /* No movement is fabricated when MainFunt skipped PhaseGet.
     * The fractional count is retained across stop/restart and reversals. */
    if ((step != 0) && (EcatPulse_Advance(&sPulsePosition, step) == 0U))
    {
        gEcatPulseDiag.positionLimit = 1U;
        gEcatMotorEnabled = 0U;
        EthercatMotorControl_EmergencyStop();
    }
    sPositionActual = EcatPulse_Position(&sPulsePosition);
    sVelocityActualRpm = step / ECAT_MOTOR_BASE_UNITS_PER_RPM;
    sPhaseFeedback = sPulsePosition.phase;
    gEcatPulseDiag.countedPosition = sPositionActual;
    step_delta = step - gEcatPulseDiag.lastPhaseStep;
    if (step_delta < 0)
        step_delta = -step_delta;
    if ((UINT32)step_delta > gEcatPulseDiag.maxStepDelta)
        gEcatPulseDiag.maxStepDelta = (UINT32)step_delta;
    gEcatPulseDiag.lastPhaseStep = step;
    if ((step > 0) && (gEcatPulseDiag.positiveStepCycles < UINT32_MAX))
        gEcatPulseDiag.positiveStepCycles++;
    else if ((step < 0) && (gEcatPulseDiag.negativeStepCycles < UINT32_MAX))
        gEcatPulseDiag.negativeStepCycles++;
    gEcatPulseDiag.atTarget = ((sPhasePrepared != 0U) &&
        (sPreparedPositionMode != 0U) &&
        (sPulsePosition.phase == sPreparedTargetPhase)) ? 1U : 0U;
    __DMB();
    sFeedbackSequence++;
}

static void EthercatMotorControl_ReadFeedback(s32 *position, s32 *velocity)
{
    UINT32 sequence;
    do
    {
        sequence = sFeedbackSequence;
        sForegroundPhase = sPhaseFeedback;
        *position = sPositionActual;
        *velocity = sVelocityActualRpm;
        __DMB();
    } while (sequence != sFeedbackSequence);
}

static void EthercatMotorControl_PublishPulseCommand(
    INT16 mode, INT32 target_position, s32 target_rpm, u8 position_mode)
{
    UINT32 next = sPulseCommandIndex ^ 1U;
    sPulseCommand[next].target_phase =
        EcatPulse_TargetPhase(target_position, sPulsePosition.resolution);
    sPulseCommand[next].target_position = target_position;
    sPulseCommand[next].base_rpm =
        target_rpm * ECAT_MOTOR_BASE_UNITS_PER_RPM;
    sPulseCommand[next].mode = mode;
    sPulseCommand[next].position_mode = position_mode;
    sPulseCommand[next].speed_limit_base =
        EthercatMotorControl_ProfileVelocityLimit(
            LocalAxes[0].Objects.objProfileVelocity) *
        ECAT_MOTOR_BASE_UNITS_PER_RPM;
    __DMB();
    sPulseCommandIndex = next;
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
    s32 actual_velocity;
    INT32 pulse_target;
    u8 position_mode;
    s32 target_rpm;
    u8 command_enabled;
    u8 target_reached;
    u8 limited;
    u8 fault_code;
    UINT16 cia_error_code;

    EthercatMotorControl_ReadFeedback(&actual_position, &actual_velocity);
    axis->Objects.objPositionActualValue = (INT32)actual_position;
    axis->Objects.objVelocityActualValue =
        (INT32)actual_velocity;
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

    if (gEscPdiDiag.faultLatched != 0U)
    {
        EthercatMotorControl_Disable();
        EthercatMotorControl_ResetProfilePosition(axis, actual_position);
        axis->Objects.objStatusWord &=
            (UINT16)~STATUSWORD_DRIVE_FOLLOWS_COMMAND;
        u16ErrorRegister |= 0x11U; /* Generic + communication error. */
        if ((axis->bAxisIsActive != FALSE) &&
            (axis->i16State != STATE_FAULT_REACTION_ACTIVE) &&
            (axis->i16State != STATE_FAULT))
            CiA402_LocalError(ERROR_COMMUNICATION);
        return;
    }

    if ((gEcatPulseDiag.positionLimit != 0U) ||
        (gEcatPulseDiag.resolutionChanged != 0U))
    {
        EthercatMotorControl_Disable();
        axis->Objects.objStatusWord |= STATUSWORD_INTERNAL_LIMIT;
        axis->Objects.objStatusWord &=
            (UINT16)~STATUSWORD_DRIVE_FOLLOWS_COMMAND;
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
        if (gEcatMotorEnabled != 0U)
        {
            if (gEcatCommDiag.driveDisableCount != UINT32_MAX)
                gEcatCommDiag.driveDisableCount++;
            if (gEcatCommDiag.firstDisableValid == 0U)
            {
                gEcatCommDiag.firstDisableControlWord = axis->Objects.objControlWord;
                gEcatCommDiag.firstDisableStatusWord = axis->Objects.objStatusWord;
                gEcatCommDiag.firstDisableAlState = nAlStatus;
                gEcatCommDiag.firstDisablePosition = actual_position;
                gEcatCommDiag.firstDisablePdiError = gEscPdiDiag.lastError;
                __DMB();
                gEcatCommDiag.firstDisableValid = 1U;
            }
        }
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

    position_mode = ((requested_mode == PROFILE_POSITION_MODE) ||
                     (requested_mode == CYCLIC_SYNC_POSITION_MODE)) ? 1U : 0U;
    pulse_target = (requested_mode == PROFILE_POSITION_MODE) ?
                   sProfilePositionTarget : cyclic_target_position;
    /* PP Halt decelerates without creeping toward its suspended endpoint. */
    if ((requested_mode == PROFILE_POSITION_MODE) &&
        ((axis->Objects.objControlWord & ECAT_CONTROLWORD_HALT) != 0U))
        position_mode = 0U;
    EthercatMotorControl_PublishPulseCommand(
        requested_mode, pulse_target, target_rpm, position_mode);
    /* Publish command first and enable last for an atomic ISR hand-off. */
    gEcatBaseRpmCommand =
        target_rpm * ECAT_MOTOR_BASE_UNITS_PER_RPM;
    gEcatMotorEnabled = 1U;
}
