#include "EthercatMotorControl.h"

#include "ecat_def.h"
#include "applInterface.h"
#include "cia402appl.h"

/* Motor-loop feedback/fault variables are owned by the existing motor code. */
extern s32 SpeedNow;
extern u8 ErrNumFlag;
extern TCiA402Axis LocalAxes[MAX_AXES];

volatile s32 gEcatBaseRpmCommand = 0;
volatile u8 gEcatMotorEnabled = 0U;

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

static void EthercatMotorControl_Disable(void)
{
    /* Clear enable first so the 16 kHz ISR cannot consume a stale command. */
    gEcatMotorEnabled = 0U;
    gEcatBaseRpmCommand = 0;
}

void EthercatMotorControl_Init(void)
{
    TCiA402Axis *axis = &LocalAxes[0];

    EthercatMotorControl_Disable();

    /* Keep firmware defaults identical to the fixed-CSV ESI description. */
    sRxPDOassign.u16SubIndex0 = 1U;
    sRxPDOassign.aEntries[0] = 0x1602U;
    sTxPDOassign.u16SubIndex0 = 1U;
    sTxPDOassign.aEntries[0] = 0x1A02U;

    sConfiguredModuleIdentList.u16SubIndex0 = 1U;
    sConfiguredModuleIdentList.aEntries[0] = CSV_MODULE_ID;
    sDetectedModuleIdentList.u16SubIndex0 = 1U;
    sDetectedModuleIdentList.aEntries[0] = CSV_MODULE_ID;

    axis->Objects.objModesOfOperation = CYCLIC_SYNC_VELOCITY_MODE;
    axis->Objects.objModesOfOperationDisplay = CYCLIC_SYNC_VELOCITY_MODE;
    axis->Objects.objSupportedDriveModes =
        (1UL << (CYCLIC_SYNC_VELOCITY_MODE - 1));
}

void EthercatMotorControl_Update(void)
{
    TCiA402Axis *axis = &LocalAxes[0];
    s32 target_rpm;
    u8 command_enabled;

    /* 0x606C uses the same rpm convention as 0x60FF. */
    axis->Objects.objVelocityActualValue =
        (INT32)(SpeedNow / ECAT_MOTOR_BASE_UNITS_PER_RPM);

    if (axis->Objects.objModesOfOperation ==
        CYCLIC_SYNC_VELOCITY_MODE)
    {
        axis->Objects.objModesOfOperationDisplay =
            CYCLIC_SYNC_VELOCITY_MODE;
    }
    else
    {
        axis->Objects.objModesOfOperationDisplay = NO_MODE;
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
        (axis->i16State == STATE_OPERATION_ENABLED) &&
        (axis->Objects.objModesOfOperation ==
         CYCLIC_SYNC_VELOCITY_MODE))
    {
        command_enabled = 1U;
    }

    if (command_enabled != 0U)
    {
        target_rpm = EthercatMotorControl_ClampRpm(
            (s32)axis->Objects.objTargetVelocity);

        /* Publish command first and enable last for an atomic hand-off. */
        gEcatBaseRpmCommand =
            target_rpm * ECAT_MOTOR_BASE_UNITS_PER_RPM;
        gEcatMotorEnabled = 1U;
        axis->Objects.objStatusWord |=
            STATUSWORD_DRIVE_FOLLOWS_COMMAND;
    }
    else
    {
        EthercatMotorControl_Disable();
        axis->Objects.objStatusWord &=
            (UINT16)~STATUSWORD_DRIVE_FOLLOWS_COMMAND;
    }
}
