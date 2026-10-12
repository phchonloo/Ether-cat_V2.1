(* Implementation pane ONLY. MAIN must be called once per 2 ms PlcTask. *)
(* Capture inputs and requests before protection code changes them. *)
bWc := GVL_CSP_FAST.iWcState;
nEcState := GVL_CSP_FAST.iEthercatState;
nStatus := GVL_CSP_FAST.iStatusWord;
nMode := GVL_CSP_FAST.iModeDisplay;
nActualPosition := GVL_CSP_FAST.iActualPosition;
nActualVelocity := GVL_CSP_FAST.iActualVelocity;
bReqEnable := GVL_CSP_FAST.bEnable;
bReqRun := GVL_CSP_FAST.bRun;
nOldControlWord := GVL_CSP_FAST.qControlWord;
nOldTargetPosition := GVL_CSP_FAST.qTargetPosition;
nOldPhase := nPhase;
bArmed := bReqEnable OR bReqRun OR bPrevEnable OR bPrevRun;
nStopMask := UINT#0;
bPlanMove := FALSE;
GVL_CSP_FAST.qMode := SINT#8;
GVL_CSP_FAST.qTargetVelocity := DINT#0;
GVL_CSP_FAST.fCommandRpm := 0.0;
IF GVL_CSP_FAST.nCycleCount < UDINT#4294967295 THEN
    GVL_CSP_FAST.nCycleCount := GVL_CSP_FAST.nCycleCount + UDINT#1;
END_IF;
GVL_CSP_FAST.bBusOk := NOT bWc AND
    ((nEcState AND UINT#16#0FFF) = UINT#8);
GVL_CSP_FAST.bFault := (nStatus AND UINT#16#0008) <> UINT#0;
GVL_CSP_FAST.bConfigError := NOT (
    (GVL_CSP_FAST.nCountsPerRev >= DINT#1) AND
    (GVL_CSP_FAST.nCountsPerRev <= cMaxCountsPerRev) AND
    (GVL_CSP_FAST.fMoveTimeSec >= cMinMoveSec) AND
    (GVL_CSP_FAST.fMoveTimeSec <= cMaxMoveSec));

(* Reset diagnostics only while disabled, stationary and communicating. *)
IF GVL_CSP_FAST.bResetDiag THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_CSP_FAST.bBusOk AND
       NOT GVL_CSP_FAST.bFault AND (nActualVelocity = DINT#0) THEN
        GVL_CSP_FAST.bDiagValid := FALSE;
        GVL_CSP_FAST.nStopReason := UINT#0;
        GVL_CSP_FAST.nStopCount := UDINT#0;
        GVL_CSP_FAST.nBadWcCycles := UDINT#0;
        GVL_CSP_FAST.nNonOpCycles := UDINT#0;
        GVL_CSP_FAST.nFaultCycles := UDINT#0;
        GVL_CSP_FAST.bStopLatched := FALSE;
        GVL_CSP_FAST.bArrivalTimeout := FALSE;
        GVL_CSP_FAST.bPositionLimit := FALSE;
    END_IF;
    GVL_CSP_FAST.bResetDiag := FALSE;
END_IF;

IF bWc THEN
    IF GVL_CSP_FAST.nBadWcCycles < UDINT#4294967295 THEN
        GVL_CSP_FAST.nBadWcCycles := GVL_CSP_FAST.nBadWcCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#1;
END_IF;
IF (nEcState AND UINT#16#0FFF) <> UINT#8 THEN
    IF GVL_CSP_FAST.nNonOpCycles < UDINT#4294967295 THEN
        GVL_CSP_FAST.nNonOpCycles := GVL_CSP_FAST.nNonOpCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#2;
END_IF;
IF GVL_CSP_FAST.bFault THEN
    IF GVL_CSP_FAST.nFaultCycles < UDINT#4294967295 THEN
        GVL_CSP_FAST.nFaultCycles := GVL_CSP_FAST.nFaultCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#4;
END_IF;
IF bPrevEnable AND NOT bReqEnable THEN
    nStopMask := nStopMask OR UINT#128;
END_IF;
IF bPrevRun AND NOT bReqRun AND bReqEnable THEN
    (* Normal stop request: complete the current turn, no new turn. *)
    nStopMask := nStopMask OR UINT#256;
END_IF;

IF NOT bReqEnable THEN
    GVL_CSP_FAST.bRun := FALSE;
END_IF;

(* CiA402 enable handshake. No automatic fault reset. *)
GVL_CSP_FAST.qControlWord := UINT#0;
GVL_CSP_FAST.bReady := FALSE;
nDriveState := nStatus AND UINT#16#006F;
IF bReqEnable AND GVL_CSP_FAST.bBusOk AND NOT GVL_CSP_FAST.bFault AND
   NOT GVL_CSP_FAST.bStopLatched THEN
    CASE nDriveState OF
        16#0040: GVL_CSP_FAST.qControlWord := UINT#6;
        16#0021: GVL_CSP_FAST.qControlWord := UINT#7;
        16#0023: GVL_CSP_FAST.qControlWord := UINT#15;
        16#0027:
            GVL_CSP_FAST.qControlWord := UINT#15;
            GVL_CSP_FAST.bReady := nMode = SINT#8;
    END_CASE;
END_IF;
IF GVL_CSP_FAST.bResetFault THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_CSP_FAST.bBusOk AND
       GVL_CSP_FAST.bFault THEN
        GVL_CSP_FAST.qControlWord := UINT#16#0080;
    END_IF;
    GVL_CSP_FAST.bResetFault := FALSE;
END_IF;

IF GVL_CSP_FAST.bBusOk AND NOT GVL_CSP_FAST.bFault AND
   NOT GVL_CSP_FAST.bStopLatched THEN
    IF bReqEnable AND NOT GVL_CSP_FAST.bReady AND
       (bPrevReady OR bReqRun) THEN
        nStopMask := nStopMask OR UINT#8;
    END_IF;
END_IF;
IF bArmed AND ((nStopMask AND UINT#127) <> UINT#0) THEN
    GVL_CSP_FAST.bStopLatched := TRUE;
END_IF;
IF GVL_CSP_FAST.bStopLatched THEN
    GVL_CSP_FAST.bEnable := FALSE;
    GVL_CSP_FAST.bRun := FALSE;
    GVL_CSP_FAST.bReady := FALSE;
    IF GVL_CSP_FAST.qControlWord <> UINT#16#0080 THEN
        GVL_CSP_FAST.qControlWord := UINT#0;
    END_IF;
END_IF;

(* On first Ready, seed the target from current feedback, never from zero. *)
IF NOT GVL_CSP_FAST.bReady OR NOT bPrevReady THEN
    IF GVL_CSP_FAST.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
    nPhase := USINT#0;
    fElapsed := 0.0;
    bTurnCounted := FALSE;
END_IF;
GVL_CSP_FAST.bEstimatedAtTarget := GVL_CSP_FAST.bReady AND
    (nActualVelocity = DINT#0) AND
    (ABS(DINT_TO_LREAL(nActualPosition) - fHold) <= cToleranceCounts);

(* Each TON is called exactly once per PLC cycle, including disabled cycles. *)
fbSettled(IN := GVL_CSP_FAST.bReady AND (nPhase = USINT#2) AND
    GVL_CSP_FAST.bEstimatedAtTarget, PT := cSettleTime);
fbPause(IN := GVL_CSP_FAST.bReady AND GVL_CSP_FAST.bRun AND
    (nPhase = USINT#3) AND GVL_CSP_FAST.bEstimatedAtTarget, PT := cPauseTime);
fbArrival(IN := GVL_CSP_FAST.bReady AND (nPhase = USINT#2),
    PT := cArrivalTimeout);

IF GVL_CSP_FAST.bReady THEN
    CASE nPhase OF
        0:
            bPlanMove := GVL_CSP_FAST.bRun AND GVL_CSP_FAST.bEstimatedAtTarget;
        1:
            fElapsed := fElapsed + cTaskSec;
            IF fElapsed >= fDuration THEN
                fElapsed := fDuration;
            END_IF;
            fU := fElapsed / fDuration;
            (* Cubic trajectory: zero commanded speed at both ends. *)
            fShape := fU * fU * (3.0 - 2.0 * fU);
            fHold := fStart + fDelta * fShape;
            GVL_CSP_FAST.fCommandRpm := 360.0 * fU * (1.0 - fU) / fDuration;
            IF fElapsed >= fDuration THEN
                fHold := fEnd;
                GVL_CSP_FAST.fCommandRpm := 0.0;
                nPhase := USINT#2;
            END_IF;
        2:
            IF fbSettled.Q THEN
                IF NOT bTurnCounted THEN
                    IF GVL_CSP_FAST.nCompletedTurns < UDINT#4294967295 THEN
                        GVL_CSP_FAST.nCompletedTurns :=
                            GVL_CSP_FAST.nCompletedTurns + UDINT#1;
                    END_IF;
                    bTurnCounted := TRUE;
                END_IF;
                IF GVL_CSP_FAST.bRun THEN
                    nPhase := USINT#3;
                ELSE
                    nPhase := USINT#0;
                END_IF;
            ELSIF fbArrival.Q THEN
                GVL_CSP_FAST.bArrivalTimeout := TRUE;
                nStopMask := nStopMask OR UINT#16;
            END_IF;
        3:
            IF NOT GVL_CSP_FAST.bRun THEN
                nPhase := USINT#0;
            ELSIF NOT GVL_CSP_FAST.bEstimatedAtTarget THEN
                nPhase := USINT#2;
            ELSIF fbPause.Q THEN
                bPlanMove := TRUE;
            END_IF;
    END_CASE;
END_IF;

IF bPlanMove THEN
    IF GVL_CSP_FAST.bConfigError THEN
        nStopMask := nStopMask OR UINT#64;
    ELSE
        (* Latch parameters for this turn. Check bounds before DINT conversion. *)
        fStart := fHold;
        fDelta := DINT_TO_LREAL(GVL_CSP_FAST.nCountsPerRev);
        fEnd := fStart + fDelta;
        IF (ABS(fStart) > cPositionLimit) OR (ABS(fEnd) > cPositionLimit) THEN
            GVL_CSP_FAST.bPositionLimit := TRUE;
            nStopMask := nStopMask OR UINT#32;
        ELSE
            fDuration := GVL_CSP_FAST.fMoveTimeSec;
            fElapsed := 0.0;
            bTurnCounted := FALSE;
            nPhase := USINT#1;
        END_IF;
    END_IF;
END_IF;

(* Also catch faults discovered in the trajectory/arrival logic this cycle. *)
IF bArmed AND ((nStopMask AND UINT#127) <> UINT#0) THEN
    GVL_CSP_FAST.bStopLatched := TRUE;
    GVL_CSP_FAST.bEnable := FALSE;
    GVL_CSP_FAST.bRun := FALSE;
    GVL_CSP_FAST.bReady := FALSE;
    GVL_CSP_FAST.qControlWord := UINT#0;
    GVL_CSP_FAST.fCommandRpm := 0.0;
    nPhase := USINT#0;
    IF GVL_CSP_FAST.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
END_IF;
IF bArmed AND (nStopMask <> UINT#0) THEN
    IF GVL_CSP_FAST.nStopCount < UDINT#4294967295 THEN
        GVL_CSP_FAST.nStopCount := GVL_CSP_FAST.nStopCount + UDINT#1;
    END_IF;
    IF NOT GVL_CSP_FAST.bDiagValid THEN
        GVL_CSP_FAST.nStopReason := nStopMask;
        GVL_CSP_FAST.diagWcState := bWc;
        GVL_CSP_FAST.diagEthercatState := nEcState;
        GVL_CSP_FAST.diagStatusWord := nStatus;
        GVL_CSP_FAST.diagModeDisplay := nMode;
        GVL_CSP_FAST.diagActualPosition := nActualPosition;
        GVL_CSP_FAST.diagActualVelocity := nActualVelocity;
        GVL_CSP_FAST.diagControlWord := nOldControlWord;
        GVL_CSP_FAST.diagTargetPosition := nOldTargetPosition;
        GVL_CSP_FAST.diagPhase := nOldPhase;
        GVL_CSP_FAST.diagEnableRequest := bReqEnable;
        GVL_CSP_FAST.diagRunRequest := bReqRun;
        GVL_CSP_FAST.diagCompletedTurns := GVL_CSP_FAST.nCompletedTurns;
        GVL_CSP_FAST.diagCycle := GVL_CSP_FAST.nCycleCount;
        (* Valid flag is written last; live fields cannot overwrite the snapshot. *)
        GVL_CSP_FAST.bDiagValid := TRUE;
    END_IF;
END_IF;
GVL_CSP_FAST.qTargetPosition := LREAL_TO_DINT(fHold);
GVL_CSP_FAST.nPhase := nPhase;
GVL_CSP_FAST.bMoving := GVL_CSP_FAST.bReady AND (nPhase = USINT#1);
GVL_CSP_FAST.bWaitingToSettle := GVL_CSP_FAST.bReady AND (nPhase = USINT#2);
GVL_CSP_FAST.bPausing := GVL_CSP_FAST.bReady AND (nPhase = USINT#3);
GVL_CSP_FAST.tPauseElapsed := fbPause.ET;
bPrevEnable := GVL_CSP_FAST.bEnable;
bPrevRun := GVL_CSP_FAST.bRun;
bPrevReady := GVL_CSP_FAST.bReady;
