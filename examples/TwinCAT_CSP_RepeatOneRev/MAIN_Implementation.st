// Phase 0: idle; 1: one revolution; 2: confirm settled; 3: pause one second.
GVL_CSP_REPEAT.qMode := SINT#8;
GVL_CSP_REPEAT.qTargetVelocity := DINT#0;
GVL_CSP_REPEAT.bBusOk := NOT GVL_CSP_REPEAT.iWcState
    AND ((GVL_CSP_REPEAT.iEthercatState AND UINT#16#0FFF) = UINT#8);
GVL_CSP_REPEAT.bFault :=
    (GVL_CSP_REPEAT.iStatusWord AND UINT#16#0008) <> UINT#0;
GVL_CSP_REPEAT.bReady := FALSE;
GVL_CSP_REPEAT.fCommandRpm := 0.0;
bPlanMove := FALSE;

// Reset a sequence alarm only while disabled with valid, stationary feedback.
IF GVL_CSP_REPEAT.bResetSequence THEN
    IF NOT GVL_CSP_REPEAT.bEnable
        AND GVL_CSP_REPEAT.bBusOk AND NOT GVL_CSP_REPEAT.bFault
        AND (GVL_CSP_REPEAT.iActualVelocity = DINT#0) THEN
        GVL_CSP_REPEAT.bArrivalTimeout := FALSE;
        GVL_CSP_REPEAT.bPositionLimit := FALSE;
    END_IF;
    GVL_CSP_REPEAT.bResetSequence := FALSE;
END_IF;

GVL_CSP_REPEAT.bConfigError := NOT (
    (GVL_CSP_REPEAT.nCountsPerRev > DINT#0)
    AND (GVL_CSP_REPEAT.nCountsPerRev <= cMaxCountsPerRev)
    AND (GVL_CSP_REPEAT.fMoveTimeSec >= cMinMoveSec)
    AND (GVL_CSP_REPEAT.fMoveTimeSec <= cMaxMoveSec));
IF GVL_CSP_REPEAT.bConfigError THEN
    GVL_CSP_REPEAT.bRun := FALSE;
END_IF;

// Faults and bus loss cancel the run request; never resume automatically.
IF NOT GVL_CSP_REPEAT.bBusOk OR GVL_CSP_REPEAT.bFault
    OR GVL_CSP_REPEAT.bArrivalTimeout OR GVL_CSP_REPEAT.bPositionLimit THEN
    GVL_CSP_REPEAT.bEnable := FALSE;
    GVL_CSP_REPEAT.bRun := FALSE;
END_IF;

IF NOT GVL_CSP_REPEAT.bEnable
    OR NOT GVL_CSP_REPEAT.bBusOk OR GVL_CSP_REPEAT.bFault THEN
    GVL_CSP_REPEAT.qControlWord := UINT#0;
    IF GVL_CSP_REPEAT.bBusOk AND GVL_CSP_REPEAT.bFault
        AND GVL_CSP_REPEAT.bResetFault THEN
        GVL_CSP_REPEAT.qControlWord := UINT#16#0080;
    END_IF;
ELSE
    nDriveState := GVL_CSP_REPEAT.iStatusWord AND UINT#16#006F;
    CASE nDriveState OF
        UINT#16#0040:
            GVL_CSP_REPEAT.qControlWord := UINT#6;
        UINT#16#0021:
            GVL_CSP_REPEAT.qControlWord := UINT#7;
        UINT#16#0023:
            GVL_CSP_REPEAT.qControlWord := UINT#15;
        UINT#16#0027:
            GVL_CSP_REPEAT.qControlWord := UINT#15;
            GVL_CSP_REPEAT.bReady := GVL_CSP_REPEAT.iModeDisplay = SINT#8;
    ELSE
        GVL_CSP_REPEAT.qControlWord := UINT#0;
    END_CASE;
END_IF;
IF NOT GVL_CSP_REPEAT.bFault THEN
    GVL_CSP_REPEAT.bResetFault := FALSE;
END_IF;

IF NOT GVL_CSP_REPEAT.bReady THEN
    // A new enable starts at feedback, not at zero or an interrupted endpoint.
    fHold := DINT_TO_LREAL(GVL_CSP_REPEAT.iActualPosition);
    nPhase := USINT#0;
    fElapsed := 0.0;
    bTurnCounted := FALSE;
    GVL_CSP_REPEAT.bRun := FALSE;
END_IF;

GVL_CSP_REPEAT.bEstimatedAtTarget := GVL_CSP_REPEAT.bReady
    AND (GVL_CSP_REPEAT.iActualVelocity = DINT#0)
    AND (ABS(DINT_TO_LREAL(GVL_CSP_REPEAT.iActualPosition) - fHold)
        <= cToleranceCounts);

// Call every timer once per task cycle, including disabled cycles.
fbSettled(
    IN := GVL_CSP_REPEAT.bReady AND (nPhase = USINT#2)
        AND GVL_CSP_REPEAT.bEstimatedAtTarget,
    PT := cSettleTime);
fbPause(
    IN := GVL_CSP_REPEAT.bReady AND GVL_CSP_REPEAT.bRun
        AND (nPhase = USINT#3),
    PT := cPauseTime);
fbArrival(
    IN := GVL_CSP_REPEAT.bReady AND (nPhase = USINT#2),
    PT := cArrivalTimeout);

IF GVL_CSP_REPEAT.bReady THEN
    CASE nPhase OF
        USINT#0:
            bPlanMove := GVL_CSP_REPEAT.bRun
                AND GVL_CSP_REPEAT.bEstimatedAtTarget;

        USINT#1:
            // A normal bRun=FALSE request finishes this revolution first.
            fElapsed := MIN(fElapsed + cTaskSec, fDuration);
            fU := fElapsed / fDuration;
            fShape := 3.0 * fU * fU - 2.0 * fU * fU * fU;
            fHold := fStart + fDelta * fShape;
            GVL_CSP_REPEAT.fCommandRpm :=
                360.0 * fU * (1.0 - fU) / fDuration;
            IF fElapsed >= fDuration THEN
                fHold := fEnd;
                GVL_CSP_REPEAT.fCommandRpm := 0.0;
                nPhase := USINT#2;
            END_IF;

        USINT#2:
            IF fbSettled.Q THEN
                IF NOT bTurnCounted THEN
                    IF GVL_CSP_REPEAT.nCompletedTurns < UDINT#16#FFFFFFFF THEN
                        GVL_CSP_REPEAT.nCompletedTurns :=
                            GVL_CSP_REPEAT.nCompletedTurns + UDINT#1;
                    END_IF;
                    bTurnCounted := TRUE;
                END_IF;
                IF GVL_CSP_REPEAT.bRun THEN
                    nPhase := USINT#3;
                ELSE
                    nPhase := USINT#0;
                END_IF;
            ELSIF fbArrival.Q THEN
                // No next revolution on stalled/invalid estimated feedback.
                GVL_CSP_REPEAT.bArrivalTimeout := TRUE;
                GVL_CSP_REPEAT.bEnable := FALSE;
                GVL_CSP_REPEAT.bRun := FALSE;
                GVL_CSP_REPEAT.bReady := FALSE;
                GVL_CSP_REPEAT.qControlWord := UINT#0;
                nPhase := USINT#0;
            END_IF;

        USINT#3:
            IF NOT GVL_CSP_REPEAT.bRun THEN
                nPhase := USINT#0;
            ELSIF NOT GVL_CSP_REPEAT.bEstimatedAtTarget THEN
                // Interrupted settling restarts the entire dwell after confirmation.
                nPhase := USINT#2;
                // bTurnCounted prevents counting this same revolution twice.
            ELSIF fbPause.Q THEN
                bPlanMove := TRUE;
            END_IF;
    END_CASE;
END_IF;

// Latch parameters once; later online edits cannot alter the current trajectory.
IF bPlanMove THEN
    fStart := fHold;
    fDelta := DINT_TO_LREAL(GVL_CSP_REPEAT.nCountsPerRev);
    fEnd := fStart + fDelta;
    IF (fEnd <= cPositionLimit) AND (fEnd >= -cPositionLimit) THEN
        fDuration := GVL_CSP_REPEAT.fMoveTimeSec;
        fElapsed := 0.0;
        bTurnCounted := FALSE;
        nPhase := USINT#1;
    ELSE
        GVL_CSP_REPEAT.bPositionLimit := TRUE;
        GVL_CSP_REPEAT.bEnable := FALSE;
        GVL_CSP_REPEAT.bRun := FALSE;
        GVL_CSP_REPEAT.bReady := FALSE;
        GVL_CSP_REPEAT.qControlWord := UINT#0;
        nPhase := USINT#0;
    END_IF;
END_IF;

GVL_CSP_REPEAT.qTargetPosition := LREAL_TO_DINT(fHold);
GVL_CSP_REPEAT.nPhase := nPhase;
GVL_CSP_REPEAT.bMoving := nPhase = USINT#1;
GVL_CSP_REPEAT.bWaitingToSettle := nPhase = USINT#2;
GVL_CSP_REPEAT.bPausing := nPhase = USINT#3;
GVL_CSP_REPEAT.tPauseElapsed := fbPause.ET;
GVL_CSP_REPEAT.bEstimatedAtTarget := GVL_CSP_REPEAT.bReady
    AND (GVL_CSP_REPEAT.iActualVelocity = DINT#0)
    AND (ABS(DINT_TO_LREAL(GVL_CSP_REPEAT.iActualPosition) - fHold)
        <= cToleranceCounts);
