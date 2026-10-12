(* Implementation pane ONLY. MAIN must be called once per 2 ms PlcTask. *)
(* Capture inputs and requests before protection code changes them. *)
bWc := GVL_ID256_CSP.iWcState;
nEcState := GVL_ID256_CSP.iEthercatState;
nStatus := GVL_ID256_CSP.iStatusWord;
nErrorCode := GVL_ID256_CSP.iErrorCode;
nDigitalInputs := GVL_ID256_CSP.iDigitalInputs;
nMode := GVL_ID256_CSP.iModeDisplay;
nActualPosition := GVL_ID256_CSP.iActualPosition;
nActualVelocity := GVL_ID256_CSP.iActualVelocity;
bReqEnable := GVL_ID256_CSP.bEnable;
bReqRun := GVL_ID256_CSP.bRun;
nOldControlWord := GVL_ID256_CSP.qControlWord;
nOldTargetPosition := GVL_ID256_CSP.qTargetPosition;
nOldPhase := nPhase;
bArmed := bReqEnable OR bReqRun OR bPrevEnable OR bPrevRun;
nStopMask := UINT#0;
bPlanMove := FALSE;
GVL_ID256_CSP.qMode := SINT#8;
GVL_ID256_CSP.qTouchProbeFunction := UINT#0;
GVL_ID256_CSP.qTargetVelocity := DINT#0;
GVL_ID256_CSP.fCommandRpm := 0.0;
IF GVL_ID256_CSP.nCycleCount < UDINT#4294967295 THEN
    GVL_ID256_CSP.nCycleCount := GVL_ID256_CSP.nCycleCount + UDINT#1;
END_IF;
GVL_ID256_CSP.bBusOk := NOT bWc AND
    ((nEcState AND UINT#16#0FFF) = UINT#8);
GVL_ID256_CSP.bFault := (nStatus AND UINT#16#0008) <> UINT#0;
GVL_ID256_CSP.bConfigError := NOT (
    (GVL_ID256_CSP.nCountsPerRev >= DINT#200) AND
    (GVL_ID256_CSP.nCountsPerRev <= cMaxCountsPerRev) AND
    (GVL_ID256_CSP.fMoveTimeSec >= cMinMoveSec) AND
    (GVL_ID256_CSP.fMoveTimeSec <= cMaxMoveSec));

(* Reset diagnostics only while disabled, stationary and communicating. *)
IF GVL_ID256_CSP.bResetDiag THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_ID256_CSP.bBusOk AND
       NOT GVL_ID256_CSP.bFault AND (nActualVelocity = DINT#0) THEN
        GVL_ID256_CSP.bDiagValid := FALSE;
        GVL_ID256_CSP.nStopReason := UINT#0;
        GVL_ID256_CSP.nStopCount := UDINT#0;
        GVL_ID256_CSP.nBadWcCycles := UDINT#0;
        GVL_ID256_CSP.nNonOpCycles := UDINT#0;
        GVL_ID256_CSP.nFaultCycles := UDINT#0;
        GVL_ID256_CSP.bStopLatched := FALSE;
        GVL_ID256_CSP.bArrivalTimeout := FALSE;
        GVL_ID256_CSP.bPositionLimit := FALSE;
    END_IF;
    GVL_ID256_CSP.bResetDiag := FALSE;
END_IF;

IF bWc THEN
    IF GVL_ID256_CSP.nBadWcCycles < UDINT#4294967295 THEN
        GVL_ID256_CSP.nBadWcCycles := GVL_ID256_CSP.nBadWcCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#1;
END_IF;
IF (nEcState AND UINT#16#0FFF) <> UINT#8 THEN
    IF GVL_ID256_CSP.nNonOpCycles < UDINT#4294967295 THEN
        GVL_ID256_CSP.nNonOpCycles := GVL_ID256_CSP.nNonOpCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#2;
END_IF;
IF GVL_ID256_CSP.bFault THEN
    IF GVL_ID256_CSP.nFaultCycles < UDINT#4294967295 THEN
        GVL_ID256_CSP.nFaultCycles := GVL_ID256_CSP.nFaultCycles + UDINT#1;
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
    GVL_ID256_CSP.bRun := FALSE;
END_IF;

(* CiA402 enable handshake. No automatic fault reset. *)
GVL_ID256_CSP.qControlWord := UINT#0;
GVL_ID256_CSP.bReady := FALSE;
nDriveState := nStatus AND UINT#16#006F;
IF bReqEnable AND GVL_ID256_CSP.bBusOk AND NOT GVL_ID256_CSP.bFault AND
   NOT GVL_ID256_CSP.bStopLatched THEN
    CASE nDriveState OF
        16#0040: GVL_ID256_CSP.qControlWord := UINT#6;
        16#0021: GVL_ID256_CSP.qControlWord := UINT#7;
        16#0023: GVL_ID256_CSP.qControlWord := UINT#15;
        16#0027:
            (* ID256 manual: run at 0x001F, then wait for follow-master bit. *)
            GVL_ID256_CSP.qControlWord := UINT#31;
            GVL_ID256_CSP.bReady := (nMode = SINT#8) AND
                ((nStatus AND UINT#16#1000) <> UINT#0);
    END_CASE;
END_IF;
IF GVL_ID256_CSP.bResetFault THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_ID256_CSP.bBusOk AND
       GVL_ID256_CSP.bFault THEN
        GVL_ID256_CSP.qControlWord := UINT#16#0080;
    END_IF;
    GVL_ID256_CSP.bResetFault := FALSE;
END_IF;

IF GVL_ID256_CSP.bBusOk AND NOT GVL_ID256_CSP.bFault AND
   NOT GVL_ID256_CSP.bStopLatched THEN
    IF bReqEnable AND NOT GVL_ID256_CSP.bReady AND
       (bPrevReady OR bReqRun) THEN
        nStopMask := nStopMask OR UINT#8;
    END_IF;
END_IF;
IF bReqEnable AND ((nStatus AND UINT#16#0100) <> UINT#0) THEN
    (* ID256 status bit 8: abnormal stop, including hardware limit/quick stop. *)
    nStopMask := nStopMask OR UINT#512;
END_IF;
IF bArmed AND ((nStopMask AND UINT#639) <> UINT#0) THEN
    GVL_ID256_CSP.bStopLatched := TRUE;
END_IF;
IF GVL_ID256_CSP.bStopLatched THEN
    GVL_ID256_CSP.bEnable := FALSE;
    GVL_ID256_CSP.bRun := FALSE;
    GVL_ID256_CSP.bReady := FALSE;
    IF GVL_ID256_CSP.qControlWord <> UINT#16#0080 THEN
        GVL_ID256_CSP.qControlWord := UINT#0;
    END_IF;
END_IF;

(* On first Ready, seed the target from current feedback, never from zero. *)
IF NOT GVL_ID256_CSP.bReady OR NOT bPrevReady THEN
    IF GVL_ID256_CSP.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
    nPhase := USINT#0;
    fElapsed := 0.0;
    bTurnCounted := FALSE;
END_IF;
GVL_ID256_CSP.bAtTarget := GVL_ID256_CSP.bReady AND
    (ABS(DINT_TO_LREAL(nActualVelocity)) <= cVelocityTolerance) AND
    (ABS(DINT_TO_LREAL(nActualPosition) - fHold) <= cToleranceCounts);

(* Each TON is called exactly once per PLC cycle, including disabled cycles. *)
fbSettled(IN := GVL_ID256_CSP.bReady AND (nPhase = USINT#2) AND
    GVL_ID256_CSP.bAtTarget, PT := cSettleTime);
fbPause(IN := GVL_ID256_CSP.bReady AND GVL_ID256_CSP.bRun AND
    (nPhase = USINT#3) AND GVL_ID256_CSP.bAtTarget, PT := cPauseTime);
fbArrival(IN := GVL_ID256_CSP.bReady AND (nPhase = USINT#2),
    PT := cArrivalTimeout);

IF GVL_ID256_CSP.bReady THEN
    CASE nPhase OF
        0:
            bPlanMove := GVL_ID256_CSP.bRun AND GVL_ID256_CSP.bAtTarget;
        1:
            fElapsed := fElapsed + cTaskSec;
            IF fElapsed >= fDuration THEN
                fElapsed := fDuration;
            END_IF;
            fU := fElapsed / fDuration;
            (* Cubic trajectory: zero commanded speed at both ends. *)
            fShape := fU * fU * (3.0 - 2.0 * fU);
            fHold := fStart + fDelta * fShape;
            GVL_ID256_CSP.fCommandRpm := 360.0 * fU * (1.0 - fU) / fDuration;
            IF fElapsed >= fDuration THEN
                fHold := fEnd;
                GVL_ID256_CSP.fCommandRpm := 0.0;
                nPhase := USINT#2;
            END_IF;
        2:
            IF fbSettled.Q THEN
                IF NOT bTurnCounted THEN
                    IF GVL_ID256_CSP.nCompletedTurns < UDINT#4294967295 THEN
                        GVL_ID256_CSP.nCompletedTurns :=
                            GVL_ID256_CSP.nCompletedTurns + UDINT#1;
                    END_IF;
                    bTurnCounted := TRUE;
                END_IF;
                IF NOT GVL_ID256_CSP.bRepeat THEN
                    GVL_ID256_CSP.bRun := FALSE;
                END_IF;
                IF GVL_ID256_CSP.bRun THEN
                    nPhase := USINT#3;
                ELSE
                    nPhase := USINT#0;
                END_IF;
            ELSIF fbArrival.Q THEN
                GVL_ID256_CSP.bArrivalTimeout := TRUE;
                nStopMask := nStopMask OR UINT#16;
            END_IF;
        3:
            IF NOT GVL_ID256_CSP.bRun THEN
                nPhase := USINT#0;
            ELSIF NOT GVL_ID256_CSP.bAtTarget THEN
                nPhase := USINT#2;
            ELSIF fbPause.Q THEN
                bPlanMove := TRUE;
            END_IF;
    END_CASE;
END_IF;

IF bPlanMove THEN
    IF GVL_ID256_CSP.bConfigError THEN
        nStopMask := nStopMask OR UINT#64;
    ELSE
        (* Latch parameters for this turn. Check bounds before DINT conversion. *)
        fStart := fHold;
        fDelta := DINT_TO_LREAL(GVL_ID256_CSP.nCountsPerRev);
        fEnd := fStart + fDelta;
        IF (ABS(fStart) > cPositionLimit) OR (ABS(fEnd) > cPositionLimit) THEN
            GVL_ID256_CSP.bPositionLimit := TRUE;
            nStopMask := nStopMask OR UINT#32;
        ELSE
            fDuration := GVL_ID256_CSP.fMoveTimeSec;
            fElapsed := 0.0;
            bTurnCounted := FALSE;
            nPhase := USINT#1;
        END_IF;
    END_IF;
END_IF;

(* Also catch faults discovered in the trajectory/arrival logic this cycle. *)
IF bArmed AND ((nStopMask AND UINT#639) <> UINT#0) THEN
    GVL_ID256_CSP.bStopLatched := TRUE;
    GVL_ID256_CSP.bEnable := FALSE;
    GVL_ID256_CSP.bRun := FALSE;
    GVL_ID256_CSP.bReady := FALSE;
    GVL_ID256_CSP.qControlWord := UINT#0;
    GVL_ID256_CSP.fCommandRpm := 0.0;
    nPhase := USINT#0;
    IF GVL_ID256_CSP.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
END_IF;
IF bArmed AND (nStopMask <> UINT#0) THEN
    IF GVL_ID256_CSP.nStopCount < UDINT#4294967295 THEN
        GVL_ID256_CSP.nStopCount := GVL_ID256_CSP.nStopCount + UDINT#1;
    END_IF;
    IF NOT GVL_ID256_CSP.bDiagValid THEN
        GVL_ID256_CSP.nStopReason := nStopMask;
        GVL_ID256_CSP.diagWcState := bWc;
        GVL_ID256_CSP.diagEthercatState := nEcState;
        GVL_ID256_CSP.diagStatusWord := nStatus;
        GVL_ID256_CSP.diagErrorCode := nErrorCode;
        GVL_ID256_CSP.diagDigitalInputs := nDigitalInputs;
        GVL_ID256_CSP.diagModeDisplay := nMode;
        GVL_ID256_CSP.diagActualPosition := nActualPosition;
        GVL_ID256_CSP.diagActualVelocity := nActualVelocity;
        GVL_ID256_CSP.diagControlWord := nOldControlWord;
        GVL_ID256_CSP.diagTargetPosition := nOldTargetPosition;
        GVL_ID256_CSP.diagPhase := nOldPhase;
        GVL_ID256_CSP.diagEnableRequest := bReqEnable;
        GVL_ID256_CSP.diagRunRequest := bReqRun;
        GVL_ID256_CSP.diagCompletedTurns := GVL_ID256_CSP.nCompletedTurns;
        GVL_ID256_CSP.diagCycle := GVL_ID256_CSP.nCycleCount;
        (* Valid flag is written last; live fields cannot overwrite the snapshot. *)
        GVL_ID256_CSP.bDiagValid := TRUE;
    END_IF;
END_IF;
GVL_ID256_CSP.qTargetPosition := LREAL_TO_DINT(fHold);
GVL_ID256_CSP.nPhase := nPhase;
GVL_ID256_CSP.bMoving := GVL_ID256_CSP.bReady AND (nPhase = USINT#1);
GVL_ID256_CSP.bWaitingToSettle := GVL_ID256_CSP.bReady AND (nPhase = USINT#2);
GVL_ID256_CSP.bPausing := GVL_ID256_CSP.bReady AND (nPhase = USINT#3);
GVL_ID256_CSP.tPauseElapsed := fbPause.ET;
bPrevEnable := GVL_ID256_CSP.bEnable;
bPrevRun := GVL_ID256_CSP.bRun;
bPrevReady := GVL_ID256_CSP.bReady;
