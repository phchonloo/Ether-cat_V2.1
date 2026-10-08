// Send CSP mode and an unused velocity target on EVERY application cycle.
GVL_CSP_1RPS.qMode := SINT#8;
GVL_CSP_1RPS.qTargetVelocity := DINT#0;
GVL_CSP_1RPS.bBusOk := NOT GVL_CSP_1RPS.iWcState
    AND ((GVL_CSP_1RPS.iEthercatState AND UINT#16#0FFF) = UINT#8);
GVL_CSP_1RPS.bFault :=
    (GVL_CSP_1RPS.iStatusWord AND UINT#16#0008) <> UINT#0;
GVL_CSP_1RPS.bReady := FALSE;

IF NOT GVL_CSP_1RPS.bFault THEN
    GVL_CSP_1RPS.bResetFault := FALSE;
END_IF;

// Communication loss or a drive fault requires explicit re-enabling.
IF NOT GVL_CSP_1RPS.bBusOk OR GVL_CSP_1RPS.bFault THEN
    GVL_CSP_1RPS.bEnable := FALSE;
    GVL_CSP_1RPS.bRun := FALSE;
END_IF;

IF NOT GVL_CSP_1RPS.bEnable
    OR NOT GVL_CSP_1RPS.bBusOk
    OR GVL_CSP_1RPS.bFault THEN

    GVL_CSP_1RPS.qControlWord := UINT#0;
    IF GVL_CSP_1RPS.bBusOk AND GVL_CSP_1RPS.bFault
        AND GVL_CSP_1RPS.bResetFault THEN
        GVL_CSP_1RPS.qControlWord := UINT#16#0080;
    END_IF;

    // Hold actual position before enable; do not start at an arbitrary zero.
    fPosition := DINT_TO_LREAL(GVL_CSP_1RPS.iActualPosition);
    fRampRpm := 0.0;
    nWindowCycles := UINT#0;
    GVL_CSP_1RPS.qTargetPosition := GVL_CSP_1RPS.iActualPosition;
    GVL_CSP_1RPS.bRun := FALSE;
    GVL_CSP_1RPS.bTrajectoryMoving := FALSE;
    GVL_CSP_1RPS.bAtOneRevPerSecond := FALSE;
    GVL_CSP_1RPS.bEstimatedStopped := GVL_CSP_1RPS.bBusOk
        AND NOT GVL_CSP_1RPS.bFault
        AND (GVL_CSP_1RPS.iActualVelocity = DINT#0);
    GVL_CSP_1RPS.bPositionLimit := fPosition >= cPositionStopLimit;
    GVL_CSP_1RPS.fCommandRpm := 0.0;
    GVL_CSP_1RPS.fIncrementCounts := 0.0;
    GVL_CSP_1RPS.bOneSecondSampleValid := FALSE;
    RETURN;
END_IF;

// Decode CiA402 state bits. CSP does not need PP's 47 -> 63 trigger.
nDriveState := GVL_CSP_1RPS.iStatusWord AND UINT#16#006F;
CASE nDriveState OF
    UINT#16#0040:
        GVL_CSP_1RPS.qControlWord := UINT#6;
    UINT#16#0021:
        GVL_CSP_1RPS.qControlWord := UINT#7;
    UINT#16#0023:
        GVL_CSP_1RPS.qControlWord := UINT#15;
    UINT#16#0027:
        GVL_CSP_1RPS.qControlWord := UINT#15;
        GVL_CSP_1RPS.bReady := GVL_CSP_1RPS.iModeDisplay = SINT#8;
ELSE
    GVL_CSP_1RPS.qControlWord := UINT#0;
END_CASE;

// Do not queue a start while waiting for mode/state confirmation.
IF NOT GVL_CSP_1RPS.bReady THEN
    fPosition := DINT_TO_LREAL(GVL_CSP_1RPS.iActualPosition);
    fRampRpm := 0.0;
    nWindowCycles := UINT#0;
    GVL_CSP_1RPS.qTargetPosition := GVL_CSP_1RPS.iActualPosition;
    GVL_CSP_1RPS.bRun := FALSE;
    GVL_CSP_1RPS.bTrajectoryMoving := FALSE;
    GVL_CSP_1RPS.bAtOneRevPerSecond := FALSE;
    GVL_CSP_1RPS.bEstimatedStopped :=
        GVL_CSP_1RPS.iActualVelocity = DINT#0;
    GVL_CSP_1RPS.bPositionLimit := fPosition >= cPositionStopLimit;
    GVL_CSP_1RPS.fCommandRpm := 0.0;
    GVL_CSP_1RPS.fIncrementCounts := 0.0;
    GVL_CSP_1RPS.bOneSecondSampleValid := FALSE;
    RETURN;
END_IF;

// This short demo does not implement cyclic position wraparound.
GVL_CSP_1RPS.bPositionLimit := fPosition >= cPositionStopLimit;
IF GVL_CSP_1RPS.bPositionLimit THEN
    GVL_CSP_1RPS.bRun := FALSE;
END_IF;

// Normal stopping keeps the drive enabled and ramps position increments down.
IF GVL_CSP_1RPS.bRun THEN
    fRampRpm := MIN(cRunRpm, fRampRpm + cAccelRpmPerSec * cTaskSec);
ELSE
    fRampRpm := MAX(0.0, fRampRpm - cDecelRpmPerSec * cTaskSec);
END_IF;

// Capture the PREVIOUS PDO target so 500 intervals give one second.
IF fRampRpm >= cRunRpm THEN
    IF nWindowCycles = UINT#0 THEN
        nWindowStart := GVL_CSP_1RPS.qTargetPosition;
    END_IF;
ELSE
    nWindowCycles := UINT#0;
    GVL_CSP_1RPS.bOneSecondSampleValid := FALSE;
END_IF;

// Preserve fractional counts until converting the complete position to DINT.
GVL_CSP_1RPS.fIncrementCounts :=
    fRampRpm / 60.0 * cCountsPerRev * cTaskSec;
fPosition := fPosition + GVL_CSP_1RPS.fIncrementCounts;
GVL_CSP_1RPS.qTargetPosition := LREAL_TO_DINT(fPosition);

IF fRampRpm >= cRunRpm THEN
    nWindowCycles := nWindowCycles + UINT#1;
    IF nWindowCycles >= cOneSecondCycles THEN
        GVL_CSP_1RPS.nOneSecondTargetDelta :=
            GVL_CSP_1RPS.qTargetPosition - nWindowStart;
        GVL_CSP_1RPS.bOneSecondSampleValid := TRUE;
        nWindowCycles := UINT#0;
    END_IF;
END_IF;

GVL_CSP_1RPS.fCommandRpm := fRampRpm;
GVL_CSP_1RPS.bTrajectoryMoving := fRampRpm > 0.0;
GVL_CSP_1RPS.bAtOneRevPerSecond :=
    GVL_CSP_1RPS.bRun AND (fRampRpm >= cRunRpm);
GVL_CSP_1RPS.bEstimatedStopped := (fRampRpm = 0.0)
    AND (GVL_CSP_1RPS.iActualVelocity = DINT#0)
    AND (ABS(DINT_TO_LREAL(GVL_CSP_1RPS.iActualPosition)
        - DINT_TO_LREAL(GVL_CSP_1RPS.qTargetPosition)) <= 2.0);
