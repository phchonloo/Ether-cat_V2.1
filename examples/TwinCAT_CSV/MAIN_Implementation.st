GVL_CSV.qMode := SINT#9;
// 0x607A is unused in the current CSV firmware branch.
GVL_CSV.qTargetPosition := GVL_CSV.iActualPosition;
GVL_CSV.bBusOk := NOT GVL_CSV.iWcState
    AND ((GVL_CSV.iEthercatState AND UINT#16#0FFF) = UINT#8);
GVL_CSV.bFault := (GVL_CSV.iStatusWord AND UINT#16#0008) <> UINT#0;
GVL_CSV.bReady := FALSE;
GVL_CSV.bAtCommandSpeed := FALSE;
GVL_CSV.bSpeedLimited := FALSE;

IF NOT GVL_CSV.bFault THEN
    GVL_CSV.bResetFault := FALSE;
END_IF;

// A bus loss or fault cancels enable and the old speed request.
IF NOT GVL_CSV.bBusOk OR GVL_CSV.bFault OR NOT GVL_CSV.bEnable THEN
    GVL_CSV.qControlWord := UINT#0;
    IF GVL_CSV.bBusOk AND GVL_CSV.bFault AND GVL_CSV.bResetFault THEN
        GVL_CSV.qControlWord := UINT#16#0080;
    END_IF;
    IF NOT GVL_CSV.bBusOk OR GVL_CSV.bFault THEN
        GVL_CSV.bEnable := FALSE;
    END_IF;
    GVL_CSV.nVelocityRequestRpm := DINT#0;
    GVL_CSV.qTargetVelocity := DINT#0;
    fCommandRpm := 0.0;
    RETURN;
END_IF;

nDriveState := GVL_CSV.iStatusWord AND UINT#16#006F;
CASE nDriveState OF
    UINT#16#0040:
        GVL_CSV.qControlWord := UINT#6;
    UINT#16#0021:
        GVL_CSV.qControlWord := UINT#7;
    UINT#16#0023:
        GVL_CSV.qControlWord := UINT#15;
    UINT#16#0027:
        GVL_CSV.qControlWord := UINT#15;
        GVL_CSV.bReady := GVL_CSV.iModeDisplay = SINT#9;
ELSE
    GVL_CSV.qControlWord := UINT#0;
END_CASE;

// Enable at zero speed. Only accept a speed request after mode 9 is ready.
IF NOT GVL_CSV.bReady THEN
    GVL_CSV.nVelocityRequestRpm := DINT#0;
    GVL_CSV.qTargetVelocity := DINT#0;
    fCommandRpm := 0.0;
    RETURN;
END_IF;

nLimitedRpm := LIMIT(-cTestLimitRpm,
                     GVL_CSV.nVelocityRequestRpm, cTestLimitRpm);
GVL_CSV.bSpeedLimited := nLimitedRpm <> GVL_CSV.nVelocityRequestRpm;
fTargetRpm := DINT_TO_LREAL(nLimitedRpm);
fRampTarget := fTargetRpm;

// For a reversal, decelerate to zero before accelerating the other way.
IF fCommandRpm * fTargetRpm < 0.0 THEN
    fRampTarget := 0.0;
    fRate := GVL_CSV.fDecelRpmPerSec;
ELSIF ABS(fTargetRpm) < ABS(fCommandRpm) THEN
    fRate := GVL_CSV.fDecelRpmPerSec;
ELSE
    fRate := GVL_CSV.fAccelRpmPerSec;
END_IF;

fStep := MAX(1.0, fRate) * cTaskSec;
fDifference := fRampTarget - fCommandRpm;
IF fDifference > fStep THEN
    fCommandRpm := fCommandRpm + fStep;
ELSIF fDifference < -fStep THEN
    fCommandRpm := fCommandRpm - fStep;
ELSE
    fCommandRpm := fRampTarget;
END_IF;

// Publish the velocity command on every task cycle, including steady speed.
GVL_CSV.qTargetVelocity := LREAL_TO_DINT(fCommandRpm);
GVL_CSV.bAtCommandSpeed := ABS(fCommandRpm - fTargetRpm) < 0.001
    AND (ABS(DINT_TO_LREAL(GVL_CSV.iActualVelocity) - fTargetRpm) <= 1.0);
