// Keep sending mode and targets through PDO on every PLC cycle.
GVL_CSP.qMode := SINT#8;
GVL_CSP.qTargetVelocity := DINT#0;
GVL_CSP.bBusOk := NOT GVL_CSP.iWcState
    AND ((GVL_CSP.iEthercatState AND UINT#16#0FFF) = UINT#8);
GVL_CSP.bFault := (GVL_CSP.iStatusWord AND UINT#16#0008) <> UINT#0;
GVL_CSP.bReady := FALSE;
GVL_CSP.bEstimatedAtTarget := GVL_CSP.bBusOk
    AND NOT GVL_CSP.bFault
    AND (ABS(DINT_TO_LREAL(GVL_CSP.iActualPosition)
             - DINT_TO_LREAL(GVL_CSP.qTargetPosition)) <= 2.0)
    AND (GVL_CSP.iActualVelocity = DINT#0);

IF NOT GVL_CSP.bFault THEN
    GVL_CSP.bResetFault := FALSE;
END_IF;

// A bus loss or drive fault cancels the enable request. Recovery needs
// another explicit bEnable write; an interrupted move is not resumed.
IF NOT GVL_CSP.bBusOk OR GVL_CSP.bFault OR NOT GVL_CSP.bEnable THEN
    GVL_CSP.qControlWord := UINT#0;
    IF GVL_CSP.bBusOk AND GVL_CSP.bFault AND GVL_CSP.bResetFault THEN
        GVL_CSP.qControlWord := UINT#16#0080;
    END_IF;
    IF NOT GVL_CSP.bBusOk OR GVL_CSP.bFault THEN
        GVL_CSP.bEnable := FALSE;
    END_IF;

    fHold := DINT_TO_LREAL(GVL_CSP.iActualPosition);
    GVL_CSP.qTargetPosition := GVL_CSP.iActualPosition;
    fElapsed := 0.0;
    bMoving := FALSE;
    GVL_CSP.bStartMove := FALSE;
    GVL_CSP.bBusy := FALSE;
    GVL_CSP.bTrajectoryDone := FALSE;
    GVL_CSP.bEstimatedAtTarget := FALSE;
    GVL_CSP.bRequestRejected := FALSE;
    RETURN;
END_IF;

// Decode state bits, excluding voltage, remote and mode-specific bits.
nDriveState := GVL_CSP.iStatusWord AND UINT#16#006F;
CASE nDriveState OF
    UINT#16#0040: // Switch on disabled -> shutdown
        GVL_CSP.qControlWord := UINT#6;
    UINT#16#0021: // Ready to switch on -> switch on
        GVL_CSP.qControlWord := UINT#7;
    UINT#16#0023: // Switched on -> enable operation
        GVL_CSP.qControlWord := UINT#15;
    UINT#16#0027: // Operation enabled
        GVL_CSP.qControlWord := UINT#15;
        GVL_CSP.bReady := GVL_CSP.iModeDisplay = SINT#8;
ELSE
    GVL_CSP.qControlWord := UINT#0;
END_CASE;

// Match the command position to feedback before enabling CSP motion.
IF NOT GVL_CSP.bReady THEN
    fHold := DINT_TO_LREAL(GVL_CSP.iActualPosition);
    GVL_CSP.qTargetPosition := GVL_CSP.iActualPosition;
    bMoving := FALSE;
    fElapsed := 0.0;
    GVL_CSP.bStartMove := FALSE;
    GVL_CSP.bBusy := FALSE;
    GVL_CSP.bTrajectoryDone := FALSE;
    RETURN;
END_IF;

// Each TRUE write is consumed once. Request parameters are latched so
// editing them while moving cannot change the trajectory mid-move.
IF GVL_CSP.bStartMove THEN
    GVL_CSP.bStartMove := FALSE;
    GVL_CSP.bRequestRejected := TRUE;
    IF NOT bMoving AND GVL_CSP.bEstimatedAtTarget THEN
        fStart := fHold;
        fDelta := DINT_TO_LREAL(GVL_CSP.nMoveCounts);
        fEnd := fStart + fDelta;
        IF (fEnd >= -2147483648.0) AND (fEnd <= 2147483647.0) THEN
            fDuration := MAX(1.0, GVL_CSP.fMoveTimeSec);
            fElapsed := 0.0;
            bMoving := TRUE;
            GVL_CSP.bTrajectoryDone := FALSE;
            GVL_CSP.bRequestRejected := FALSE;
        END_IF;
    END_IF;
END_IF;

// Cubic position trajectory: zero commanded speed at both endpoints.
IF bMoving THEN
    fElapsed := MIN(fElapsed + cTaskSec, fDuration);
    fU := fElapsed / fDuration;
    fShape := 3.0 * fU * fU - 2.0 * fU * fU * fU;
    fHold := fStart + fDelta * fShape;
    IF fElapsed >= fDuration THEN
        fHold := fEnd;
        bMoving := FALSE;
        GVL_CSP.bTrajectoryDone := TRUE;
    END_IF;
END_IF;

GVL_CSP.qTargetPosition := LREAL_TO_DINT(fHold);
GVL_CSP.bBusy := bMoving;
