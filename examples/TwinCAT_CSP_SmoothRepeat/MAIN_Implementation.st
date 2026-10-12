(* Implementation pane ONLY. MAIN must be called once per 2 ms PlcTask. *)
(* Capture inputs and requests before protection code changes them. *)
bWc := GVL_CSP_SMOOTH.iWcState;
nEcState := GVL_CSP_SMOOTH.iEthercatState;
nStatus := GVL_CSP_SMOOTH.iStatusWord;
nMode := GVL_CSP_SMOOTH.iModeDisplay;
nActualPosition := GVL_CSP_SMOOTH.iActualPosition;
nActualVelocity := GVL_CSP_SMOOTH.iActualVelocity;
bReqEnable := GVL_CSP_SMOOTH.bEnable;
bReqRun := GVL_CSP_SMOOTH.bRun;
nOldControlWord := GVL_CSP_SMOOTH.qControlWord;
nOldTargetPosition := GVL_CSP_SMOOTH.qTargetPosition;
nOldPhase := nPhase;
bArmed := bReqEnable OR bReqRun OR bPrevEnable OR bPrevRun;
nStopMask := UINT#0;
bPlanMove := FALSE;
GVL_CSP_SMOOTH.qMode := SINT#8;
GVL_CSP_SMOOTH.qTargetVelocity := DINT#0;
GVL_CSP_SMOOTH.fCommandRpm := 0.0;
IF GVL_CSP_SMOOTH.nCycleCount < UDINT#4294967295 THEN
    GVL_CSP_SMOOTH.nCycleCount := GVL_CSP_SMOOTH.nCycleCount + UDINT#1;
END_IF;
GVL_CSP_SMOOTH.bBusOk := NOT bWc AND
    ((nEcState AND UINT#16#0FFF) = UINT#8);
GVL_CSP_SMOOTH.bFault := (nStatus AND UINT#16#0008) <> UINT#0;
GVL_CSP_SMOOTH.bConfigError := NOT (
    (GVL_CSP_SMOOTH.nCountsPerRev >= DINT#1) AND
    (GVL_CSP_SMOOTH.nCountsPerRev <= cMaxCountsPerRev) AND
    (GVL_CSP_SMOOTH.fMoveTimeSec >= cMinMoveSec) AND
    (GVL_CSP_SMOOTH.fMoveTimeSec <= cMaxMoveSec));
IF NOT GVL_CSP_SMOOTH.bConfigError THEN
    GVL_CSP_SMOOTH.fPeakRpm := 112.5 / GVL_CSP_SMOOTH.fMoveTimeSec;
    GVL_CSP_SMOOTH.fPeakAcceleration := 346.410162 /
        (GVL_CSP_SMOOTH.fMoveTimeSec * GVL_CSP_SMOOTH.fMoveTimeSec);
    GVL_CSP_SMOOTH.bConfigError :=
        (GVL_CSP_SMOOTH.fPeakRpm > cProfileRpm) OR
        (GVL_CSP_SMOOTH.fPeakAcceleration > cProfileAcceleration);
END_IF;

(* Reset diagnostics only while disabled, stationary and communicating. *)
IF GVL_CSP_SMOOTH.bResetDiag THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_CSP_SMOOTH.bBusOk AND
       NOT GVL_CSP_SMOOTH.bFault AND (nActualVelocity = DINT#0) THEN
        GVL_CSP_SMOOTH.bDiagValid := FALSE;
        GVL_CSP_SMOOTH.nStopReason := UINT#0;
        GVL_CSP_SMOOTH.nStopCount := UDINT#0;
        GVL_CSP_SMOOTH.nBadWcCycles := UDINT#0;
        GVL_CSP_SMOOTH.nNonOpCycles := UDINT#0;
        GVL_CSP_SMOOTH.nFaultCycles := UDINT#0;
        GVL_CSP_SMOOTH.bStopLatched := FALSE;
        GVL_CSP_SMOOTH.bArrivalTimeout := FALSE;
        GVL_CSP_SMOOTH.bEnableTimeout := FALSE;
        GVL_CSP_SMOOTH.bPositionLimit := FALSE;
    END_IF;
    GVL_CSP_SMOOTH.bResetDiag := FALSE;
END_IF;

IF bWc THEN
    IF GVL_CSP_SMOOTH.nBadWcCycles < UDINT#4294967295 THEN
        GVL_CSP_SMOOTH.nBadWcCycles := GVL_CSP_SMOOTH.nBadWcCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#1;
END_IF;
IF (nEcState AND UINT#16#0FFF) <> UINT#8 THEN
    IF GVL_CSP_SMOOTH.nNonOpCycles < UDINT#4294967295 THEN
        GVL_CSP_SMOOTH.nNonOpCycles := GVL_CSP_SMOOTH.nNonOpCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#2;
END_IF;
IF GVL_CSP_SMOOTH.bFault THEN
    IF GVL_CSP_SMOOTH.nFaultCycles < UDINT#4294967295 THEN
        GVL_CSP_SMOOTH.nFaultCycles := GVL_CSP_SMOOTH.nFaultCycles + UDINT#1;
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
    GVL_CSP_SMOOTH.bRun := FALSE;
END_IF;

(* CiA402 enable handshake. No automatic fault reset. *)
GVL_CSP_SMOOTH.qControlWord := UINT#0;
GVL_CSP_SMOOTH.bReady := FALSE;
nDriveState := nStatus AND UINT#16#006F;
IF bReqEnable AND GVL_CSP_SMOOTH.bBusOk AND NOT GVL_CSP_SMOOTH.bFault AND
   NOT GVL_CSP_SMOOTH.bStopLatched THEN
    CASE nDriveState OF
        16#0040: GVL_CSP_SMOOTH.qControlWord := UINT#6;
        16#0021: GVL_CSP_SMOOTH.qControlWord := UINT#7;
        16#0023: GVL_CSP_SMOOTH.qControlWord := UINT#15;
        16#0027:
            GVL_CSP_SMOOTH.qControlWord := UINT#15;
            GVL_CSP_SMOOTH.bReady := (nMode = SINT#8) AND
                ((nStatus AND UINT#16#1000) <> UINT#0);
    END_CASE;
END_IF;
IF GVL_CSP_SMOOTH.bResetFault THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_CSP_SMOOTH.bBusOk AND
       GVL_CSP_SMOOTH.bFault THEN
        GVL_CSP_SMOOTH.qControlWord := UINT#16#0080;
    END_IF;
    GVL_CSP_SMOOTH.bResetFault := FALSE;
END_IF;

IF GVL_CSP_SMOOTH.bBusOk AND NOT GVL_CSP_SMOOTH.bFault AND
   NOT GVL_CSP_SMOOTH.bStopLatched THEN
    IF bReqEnable AND NOT GVL_CSP_SMOOTH.bReady AND
       bPrevReady THEN
        nStopMask := nStopMask OR UINT#8;
    END_IF;
END_IF;
IF bArmed AND ((nStopMask AND UINT#639) <> UINT#0) THEN
    GVL_CSP_SMOOTH.bStopLatched := TRUE;
END_IF;
IF GVL_CSP_SMOOTH.bStopLatched THEN
    GVL_CSP_SMOOTH.bEnable := FALSE;
    GVL_CSP_SMOOTH.bRun := FALSE;
    GVL_CSP_SMOOTH.bReady := FALSE;
    IF GVL_CSP_SMOOTH.qControlWord <> UINT#16#0080 THEN
        GVL_CSP_SMOOTH.qControlWord := UINT#0;
    END_IF;
END_IF;

(* On first Ready, seed the target from current feedback, never from zero. *)
IF NOT GVL_CSP_SMOOTH.bReady OR NOT bPrevReady THEN
    IF GVL_CSP_SMOOTH.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
    nPhase := USINT#0;
    fElapsed := 0.0;
    nMotionTicks := UDINT#0;
    bTurnCounted := FALSE;
END_IF;
GVL_CSP_SMOOTH.bEstimatedAtTarget := GVL_CSP_SMOOTH.bReady AND
    (nActualVelocity = DINT#0) AND
    (nActualPosition = LREAL_TO_DINT(fHold));
GVL_CSP_SMOOTH.bDriveAtTarget := GVL_CSP_SMOOTH.bEstimatedAtTarget AND
    ((nStatus AND UINT#16#0400) <> UINT#0);

(* Each TON is called exactly once per PLC cycle, including disabled cycles. *)
fbSettled(IN := GVL_CSP_SMOOTH.bReady AND (nPhase = USINT#2) AND
    GVL_CSP_SMOOTH.bDriveAtTarget, PT := cSettleTime);
fbPause(IN := GVL_CSP_SMOOTH.bReady AND GVL_CSP_SMOOTH.bRun AND
    (nPhase = USINT#3) AND GVL_CSP_SMOOTH.bDriveAtTarget, PT := cPauseTime);
fbArrival(IN := GVL_CSP_SMOOTH.bReady AND (nPhase = USINT#2),
    PT := cArrivalTimeout);
fbEnable(IN := bReqEnable AND GVL_CSP_SMOOTH.bBusOk AND
    NOT GVL_CSP_SMOOTH.bFault AND NOT GVL_CSP_SMOOTH.bReady AND
    NOT GVL_CSP_SMOOTH.bStopLatched, PT := cEnableTimeout);
IF fbEnable.Q THEN
    GVL_CSP_SMOOTH.bEnableTimeout := TRUE;
    nStopMask := nStopMask OR UINT#512;
END_IF;

IF GVL_CSP_SMOOTH.bReady THEN
    CASE nPhase OF
        0:
            bPlanMove := GVL_CSP_SMOOTH.bRun AND GVL_CSP_SMOOTH.bDriveAtTarget;
        1:
            IF nMotionTicks < nTotalTicks THEN
                nMotionTicks := nMotionTicks + UDINT#1;
            END_IF;
            fElapsed := UDINT_TO_LREAL(nMotionTicks) * cTaskSec;
            fU := UDINT_TO_LREAL(nMotionTicks) / UDINT_TO_LREAL(nTotalTicks);
            (* Quintic: position, speed and acceleration join continuously.
             * Integer ticks remove accumulated floating-point time drift. *)
            fShape := fU * fU * fU * (10.0 + fU * (-15.0 + 6.0 * fU));
            IF fShape < 0.0 THEN fShape := 0.0; END_IF;
            IF fShape > 1.0 THEN fShape := 1.0; END_IF;
            nCandidateTarget := LREAL_TO_DINT(fStart + fDelta * fShape);
            (* Never reverse a positive turn due to rounding or feedback reseeding. *)
            IF nCandidateTarget < GVL_CSP_SMOOTH.qTargetPosition THEN
                nCandidateTarget := GVL_CSP_SMOOTH.qTargetPosition;
            END_IF;
            IF nCandidateTarget > LREAL_TO_DINT(fEnd) THEN
                nCandidateTarget := LREAL_TO_DINT(fEnd);
            END_IF;
            fHold := DINT_TO_LREAL(nCandidateTarget);
            GVL_CSP_SMOOTH.fCommandRpm :=
                1800.0 * fU * fU * (1.0 - fU) * (1.0 - fU) / fDuration;
            IF nMotionTicks >= nTotalTicks THEN
                fHold := fEnd;
                GVL_CSP_SMOOTH.fCommandRpm := 0.0;
                nPhase := USINT#2;
            END_IF;
        2:
            IF fbSettled.Q THEN
                IF NOT bTurnCounted THEN
                    IF GVL_CSP_SMOOTH.nCompletedTurns < UDINT#4294967295 THEN
                        GVL_CSP_SMOOTH.nCompletedTurns :=
                            GVL_CSP_SMOOTH.nCompletedTurns + UDINT#1;
                    END_IF;
                    bTurnCounted := TRUE;
                END_IF;
                IF GVL_CSP_SMOOTH.bRun THEN
                    nPhase := USINT#3;
                ELSE
                    nPhase := USINT#0;
                END_IF;
            ELSIF fbArrival.Q THEN
                GVL_CSP_SMOOTH.bArrivalTimeout := TRUE;
                nStopMask := nStopMask OR UINT#16;
            END_IF;
        3:
            IF NOT GVL_CSP_SMOOTH.bRun THEN
                nPhase := USINT#0;
            ELSIF NOT GVL_CSP_SMOOTH.bDriveAtTarget THEN
                nPhase := USINT#2;
            ELSIF fbPause.Q THEN
                bPlanMove := TRUE;
            END_IF;
    END_CASE;
END_IF;

IF bPlanMove THEN
    IF GVL_CSP_SMOOTH.bConfigError THEN
        nStopMask := nStopMask OR UINT#64;
    ELSE
        (* Latch parameters for this turn. Check bounds before DINT conversion. *)
        fStart := fHold;
        fDelta := DINT_TO_LREAL(GVL_CSP_SMOOTH.nCountsPerRev);
        fEnd := fStart + fDelta;
        IF (ABS(fStart) > cPositionLimit) OR (ABS(fEnd) > cPositionLimit) THEN
            GVL_CSP_SMOOTH.bPositionLimit := TRUE;
            nStopMask := nStopMask OR UINT#32;
        ELSE
            nTotalTicks := LREAL_TO_UDINT(GVL_CSP_SMOOTH.fMoveTimeSec / cTaskSec);
            fDuration := UDINT_TO_LREAL(nTotalTicks) * cTaskSec;
            nMotionTicks := UDINT#0;
            fElapsed := 0.0;
            bTurnCounted := FALSE;
            nPhase := USINT#1;
        END_IF;
    END_IF;
END_IF;

(* Also catch faults discovered in the trajectory/arrival logic this cycle. *)
IF bArmed AND ((nStopMask AND UINT#639) <> UINT#0) THEN
    GVL_CSP_SMOOTH.bStopLatched := TRUE;
    GVL_CSP_SMOOTH.bEnable := FALSE;
    GVL_CSP_SMOOTH.bRun := FALSE;
    GVL_CSP_SMOOTH.bReady := FALSE;
    GVL_CSP_SMOOTH.qControlWord := UINT#0;
    GVL_CSP_SMOOTH.fCommandRpm := 0.0;
    nPhase := USINT#0;
    IF GVL_CSP_SMOOTH.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
END_IF;
IF bArmed AND (nStopMask <> UINT#0) THEN
    IF GVL_CSP_SMOOTH.nStopCount < UDINT#4294967295 THEN
        GVL_CSP_SMOOTH.nStopCount := GVL_CSP_SMOOTH.nStopCount + UDINT#1;
    END_IF;
    IF NOT GVL_CSP_SMOOTH.bDiagValid OR
       (((nStopMask AND UINT#639) <> UINT#0) AND
        ((GVL_CSP_SMOOTH.nStopReason AND UINT#639) = UINT#0)) THEN
        GVL_CSP_SMOOTH.nStopReason := nStopMask;
        GVL_CSP_SMOOTH.diagWcState := bWc;
        GVL_CSP_SMOOTH.diagEthercatState := nEcState;
        GVL_CSP_SMOOTH.diagStatusWord := nStatus;
        GVL_CSP_SMOOTH.diagModeDisplay := nMode;
        GVL_CSP_SMOOTH.diagActualPosition := nActualPosition;
        GVL_CSP_SMOOTH.diagActualVelocity := nActualVelocity;
        GVL_CSP_SMOOTH.diagControlWord := nOldControlWord;
        GVL_CSP_SMOOTH.diagTargetPosition := nOldTargetPosition;
        GVL_CSP_SMOOTH.diagPhase := nOldPhase;
        GVL_CSP_SMOOTH.diagEnableRequest := bReqEnable;
        GVL_CSP_SMOOTH.diagRunRequest := bReqRun;
        GVL_CSP_SMOOTH.diagCompletedTurns := GVL_CSP_SMOOTH.nCompletedTurns;
        GVL_CSP_SMOOTH.diagCycle := GVL_CSP_SMOOTH.nCycleCount;
        (* Valid flag is written last; live fields cannot overwrite the snapshot. *)
        GVL_CSP_SMOOTH.bDiagValid := TRUE;
    END_IF;
END_IF;
GVL_CSP_SMOOTH.qTargetPosition := LREAL_TO_DINT(fHold);
GVL_CSP_SMOOTH.fFollowingErrorCounts :=
    DINT_TO_LREAL(GVL_CSP_SMOOTH.qTargetPosition) -
    DINT_TO_LREAL(nActualPosition);
GVL_CSP_SMOOTH.nPhase := nPhase;
GVL_CSP_SMOOTH.bMoving := GVL_CSP_SMOOTH.bReady AND (nPhase = USINT#1);
GVL_CSP_SMOOTH.bWaitingToSettle := GVL_CSP_SMOOTH.bReady AND (nPhase = USINT#2);
GVL_CSP_SMOOTH.bPausing := GVL_CSP_SMOOTH.bReady AND (nPhase = USINT#3);
GVL_CSP_SMOOTH.tPauseElapsed := fbPause.ET;
bPrevEnable := GVL_CSP_SMOOTH.bEnable;
bPrevRun := GVL_CSP_SMOOTH.bRun;
bPrevReady := GVL_CSP_SMOOTH.bReady;
