(* Implementation pane ONLY. MAIN must be called once per 2 ms PlcTask. *)
(* Capture inputs and requests before protection code changes them. *)
bWc := GVL_CSP_NEW.iWcState;
nEcState := GVL_CSP_NEW.iEthercatState;
bMasterDiagLinked := GVL_CSP_NEW.bMasterDiagLinked;
nMasterDevState := GVL_CSP_NEW.iMasterDevState;
nMasterFrameWcState := GVL_CSP_NEW.iMasterFrameWcState;
nMasterFrameState := GVL_CSP_NEW.iMasterFrameState;
nStatus := GVL_CSP_NEW.iStatusWord;
nMode := GVL_CSP_NEW.iModeDisplay;
nActualPosition := GVL_CSP_NEW.iActualPosition;
nActualVelocity := GVL_CSP_NEW.iActualVelocity;
bReqEnable := GVL_CSP_NEW.bEnable;
bReqRun := GVL_CSP_NEW.bRun;
nOldControlWord := GVL_CSP_NEW.qControlWord;
nOldTargetPosition := GVL_CSP_NEW.qTargetPosition;
nOldPhase := nPhase;
bArmed := bReqEnable OR bReqRun OR bPrevEnable OR bPrevRun;
nStopMask := UINT#0;
bPlanMove := FALSE;
GVL_CSP_NEW.qMode := SINT#8;
GVL_CSP_NEW.qTargetVelocity := DINT#0;
GVL_CSP_NEW.fCommandRpm := 0.0;
IF GVL_CSP_NEW.nCycleCount < UDINT#4294967295 THEN
    GVL_CSP_NEW.nCycleCount := GVL_CSP_NEW.nCycleCount + UDINT#1;
END_IF;
GVL_CSP_NEW.bBusOk := NOT bWc AND
    ((nEcState AND UINT#16#0FFF) = UINT#8);
GVL_CSP_NEW.bFault := (nStatus AND UINT#16#0008) <> UINT#0;
GVL_CSP_NEW.bConfigError := NOT (
    (GVL_CSP_NEW.nCountsPerRev >= DINT#1) AND
    (GVL_CSP_NEW.nCountsPerRev <= cMaxCountsPerRev) AND
    (GVL_CSP_NEW.fMoveTimeSec >= cMinMoveSec) AND
    (GVL_CSP_NEW.fMoveTimeSec <= cMaxMoveSec) AND
    (GVL_CSP_NEW.tPauseTime >= T#0ms) AND
    (GVL_CSP_NEW.tPauseTime <= T#60s));
IF NOT GVL_CSP_NEW.bConfigError THEN
    GVL_CSP_NEW.fPeakRpm := 112.5 / GVL_CSP_NEW.fMoveTimeSec;
    GVL_CSP_NEW.fPeakAcceleration := 346.410162 /
        (GVL_CSP_NEW.fMoveTimeSec * GVL_CSP_NEW.fMoveTimeSec);
    GVL_CSP_NEW.bConfigError :=
        (GVL_CSP_NEW.fPeakRpm > cProfileRpm) OR
        (GVL_CSP_NEW.fPeakAcceleration > cProfileAcceleration);
END_IF;

(* Reset diagnostics only while disabled, stationary and communicating. *)
IF GVL_CSP_NEW.bResetDiag THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_CSP_NEW.bBusOk AND
       NOT GVL_CSP_NEW.bFault AND (nActualVelocity = DINT#0) THEN
        GVL_CSP_NEW.bDiagValid := FALSE;
        GVL_CSP_NEW.diagMasterDiagValid := FALSE;
        GVL_CSP_NEW.nFrameMissingCycles := UDINT#0;
        GVL_CSP_NEW.nCommandWcErrorCycles := UDINT#0;
        GVL_CSP_NEW.nStopReason := UINT#0;
        GVL_CSP_NEW.nStopCount := UDINT#0;
        GVL_CSP_NEW.nBadWcCycles := UDINT#0;
        GVL_CSP_NEW.nNonOpCycles := UDINT#0;
        GVL_CSP_NEW.nFaultCycles := UDINT#0;
        GVL_CSP_NEW.nWcBadStreak := UDINT#0;
        GVL_CSP_NEW.nWcBadStreakMax := UDINT#0;
        GVL_CSP_NEW.nWcEpisodeCount := UDINT#0;
        GVL_CSP_NEW.nLastWcEpisodeCycles := UDINT#0;
        GVL_CSP_NEW.diagIssuedControlWord := UINT#0;
        GVL_CSP_NEW.bStopLatched := FALSE;
        GVL_CSP_NEW.bArrivalTimeout := FALSE;
        GVL_CSP_NEW.bEnableTimeout := FALSE;
        GVL_CSP_NEW.bPositionLimit := FALSE;
    END_IF;
    GVL_CSP_NEW.bResetDiag := FALSE;
END_IF;

(* Optional frame diagnostics do not weaken or replace the slave WcState guard. *)
IF bMasterDiagLinked THEN
    IF (nMasterFrameWcState AND UINT#16#8000) <> UINT#0 THEN
        IF GVL_CSP_NEW.nFrameMissingCycles < UDINT#4294967295 THEN
            GVL_CSP_NEW.nFrameMissingCycles := GVL_CSP_NEW.nFrameMissingCycles + UDINT#1;
        END_IF;
    END_IF;
    IF (nMasterFrameWcState AND UINT#16#7FFF) <> UINT#0 THEN
        IF GVL_CSP_NEW.nCommandWcErrorCycles < UDINT#4294967295 THEN
            GVL_CSP_NEW.nCommandWcErrorCycles := GVL_CSP_NEW.nCommandWcErrorCycles + UDINT#1;
        END_IF;
    END_IF;
END_IF;

IF bWc THEN
    (* Count consecutive bad PLC scans separately from total bad scans.
     * A single bad scan still trips immediately; do not use stale feedback. *)
    IF GVL_CSP_NEW.nWcBadStreak = UDINT#0 THEN
        IF GVL_CSP_NEW.nWcEpisodeCount < UDINT#4294967295 THEN
            GVL_CSP_NEW.nWcEpisodeCount := GVL_CSP_NEW.nWcEpisodeCount + UDINT#1;
        END_IF;
    END_IF;
    IF GVL_CSP_NEW.nWcBadStreak < UDINT#4294967295 THEN
        GVL_CSP_NEW.nWcBadStreak := GVL_CSP_NEW.nWcBadStreak + UDINT#1;
    END_IF;
    IF GVL_CSP_NEW.nWcBadStreak > GVL_CSP_NEW.nWcBadStreakMax THEN
        GVL_CSP_NEW.nWcBadStreakMax := GVL_CSP_NEW.nWcBadStreak;
    END_IF;
    IF GVL_CSP_NEW.nBadWcCycles < UDINT#4294967295 THEN
        GVL_CSP_NEW.nBadWcCycles := GVL_CSP_NEW.nBadWcCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#1;
ELSE
    IF GVL_CSP_NEW.nWcBadStreak > UDINT#0 THEN
        GVL_CSP_NEW.nLastWcEpisodeCycles := GVL_CSP_NEW.nWcBadStreak;
        GVL_CSP_NEW.nWcBadStreak := UDINT#0;
    END_IF;
END_IF;
IF (nEcState AND UINT#16#0FFF) <> UINT#8 THEN
    IF GVL_CSP_NEW.nNonOpCycles < UDINT#4294967295 THEN
        GVL_CSP_NEW.nNonOpCycles := GVL_CSP_NEW.nNonOpCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#2;
END_IF;
IF GVL_CSP_NEW.bFault THEN
    IF GVL_CSP_NEW.nFaultCycles < UDINT#4294967295 THEN
        GVL_CSP_NEW.nFaultCycles := GVL_CSP_NEW.nFaultCycles + UDINT#1;
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
    GVL_CSP_NEW.bRun := FALSE;
END_IF;

(* CiA402 enable handshake. No automatic fault reset. *)
GVL_CSP_NEW.qControlWord := UINT#0;
GVL_CSP_NEW.bReady := FALSE;
nDriveState := nStatus AND UINT#16#006F;
IF bReqEnable AND GVL_CSP_NEW.bBusOk AND NOT GVL_CSP_NEW.bFault AND
   NOT GVL_CSP_NEW.bStopLatched THEN
    CASE nDriveState OF
        16#0040: GVL_CSP_NEW.qControlWord := UINT#6;
        16#0021: GVL_CSP_NEW.qControlWord := UINT#7;
        16#0023: GVL_CSP_NEW.qControlWord := UINT#15;
        16#0027:
            GVL_CSP_NEW.qControlWord := UINT#15;
            GVL_CSP_NEW.bReady := (nMode = SINT#8) AND
                ((nStatus AND UINT#16#1000) <> UINT#0);
    END_CASE;
END_IF;
IF GVL_CSP_NEW.bResetFault THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_CSP_NEW.bBusOk AND
       GVL_CSP_NEW.bFault THEN
        GVL_CSP_NEW.qControlWord := UINT#16#0080;
    END_IF;
    GVL_CSP_NEW.bResetFault := FALSE;
END_IF;

IF GVL_CSP_NEW.bBusOk AND NOT GVL_CSP_NEW.bFault AND
   NOT GVL_CSP_NEW.bStopLatched THEN
    IF bReqEnable AND NOT GVL_CSP_NEW.bReady AND
       bPrevReady THEN
        nStopMask := nStopMask OR UINT#8;
    END_IF;
END_IF;
IF bArmed AND ((nStopMask AND UINT#639) <> UINT#0) THEN
    GVL_CSP_NEW.bStopLatched := TRUE;
END_IF;
IF GVL_CSP_NEW.bStopLatched THEN
    GVL_CSP_NEW.bEnable := FALSE;
    GVL_CSP_NEW.bRun := FALSE;
    GVL_CSP_NEW.bReady := FALSE;
    IF GVL_CSP_NEW.qControlWord <> UINT#16#0080 THEN
        GVL_CSP_NEW.qControlWord := UINT#0;
    END_IF;
END_IF;

(* On first Ready, seed the target from current feedback, never from zero. *)
IF NOT GVL_CSP_NEW.bReady OR NOT bPrevReady THEN
    IF GVL_CSP_NEW.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
    nPhase := USINT#0;
    fElapsed := 0.0;
    nMotionTicks := UDINT#0;
    bTurnCounted := FALSE;
END_IF;
GVL_CSP_NEW.bEstimatedAtTarget := GVL_CSP_NEW.bReady AND
    (nActualVelocity = DINT#0) AND
    (nActualPosition = LREAL_TO_DINT(fHold));
GVL_CSP_NEW.bDriveAtTarget := GVL_CSP_NEW.bEstimatedAtTarget AND
    ((nStatus AND UINT#16#0400) <> UINT#0);

(* Each TON is called exactly once per PLC cycle, including disabled cycles. *)
fbSettled(IN := GVL_CSP_NEW.bReady AND (nPhase = USINT#2) AND
    GVL_CSP_NEW.bDriveAtTarget, PT := cSettleTime);
fbPause(IN := GVL_CSP_NEW.bReady AND GVL_CSP_NEW.bRun AND
    (nPhase = USINT#3) AND GVL_CSP_NEW.bDriveAtTarget, PT := tLatchedPause);
fbArrival(IN := GVL_CSP_NEW.bReady AND (nPhase = USINT#2),
    PT := cArrivalTimeout);
fbEnable(IN := bReqEnable AND GVL_CSP_NEW.bBusOk AND
    NOT GVL_CSP_NEW.bFault AND NOT GVL_CSP_NEW.bReady AND
    NOT GVL_CSP_NEW.bStopLatched, PT := cEnableTimeout);
IF fbEnable.Q THEN
    GVL_CSP_NEW.bEnableTimeout := TRUE;
    nStopMask := nStopMask OR UINT#512;
END_IF;

IF GVL_CSP_NEW.bReady THEN
    CASE nPhase OF
        0:
            bPlanMove := GVL_CSP_NEW.bRun AND GVL_CSP_NEW.bDriveAtTarget;
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
            IF nCandidateTarget < GVL_CSP_NEW.qTargetPosition THEN
                nCandidateTarget := GVL_CSP_NEW.qTargetPosition;
            END_IF;
            IF nCandidateTarget > LREAL_TO_DINT(fEnd) THEN
                nCandidateTarget := LREAL_TO_DINT(fEnd);
            END_IF;
            fHold := DINT_TO_LREAL(nCandidateTarget);
            GVL_CSP_NEW.fCommandRpm :=
                1800.0 * fU * fU * (1.0 - fU) * (1.0 - fU) / fDuration;
            IF nMotionTicks >= nTotalTicks THEN
                fHold := fEnd;
                GVL_CSP_NEW.fCommandRpm := 0.0;
                nPhase := USINT#2;
            END_IF;
        2:
            IF fbSettled.Q THEN
                IF NOT bTurnCounted THEN
                    IF GVL_CSP_NEW.nCompletedTurns < UDINT#4294967295 THEN
                        GVL_CSP_NEW.nCompletedTurns :=
                            GVL_CSP_NEW.nCompletedTurns + UDINT#1;
                    END_IF;
                    bTurnCounted := TRUE;
                END_IF;
                IF NOT GVL_CSP_NEW.bRepeat THEN
                    GVL_CSP_NEW.bRun := FALSE;
                END_IF;
                IF GVL_CSP_NEW.bRun THEN
                    nPhase := USINT#3;
                ELSE
                    nPhase := USINT#0;
                END_IF;
            ELSIF fbArrival.Q THEN
                GVL_CSP_NEW.bArrivalTimeout := TRUE;
                nStopMask := nStopMask OR UINT#16;
            END_IF;
        3:
            IF NOT GVL_CSP_NEW.bRun THEN
                nPhase := USINT#0;
            ELSIF NOT GVL_CSP_NEW.bDriveAtTarget THEN
                nPhase := USINT#2;
            ELSIF fbPause.Q THEN
                bPlanMove := TRUE;
            END_IF;
    END_CASE;
END_IF;

IF bPlanMove THEN
    IF GVL_CSP_NEW.bConfigError THEN
        nStopMask := nStopMask OR UINT#64;
    ELSE
        (* Latch parameters for this turn. Check bounds before DINT conversion. *)
        fStart := fHold;
        fDelta := DINT_TO_LREAL(GVL_CSP_NEW.nCountsPerRev);
        fEnd := fStart + fDelta;
        IF (ABS(fStart) > cPositionLimit) OR (ABS(fEnd) > cPositionLimit) THEN
            GVL_CSP_NEW.bPositionLimit := TRUE;
            nStopMask := nStopMask OR UINT#32;
        ELSE
            nTotalTicks := LREAL_TO_UDINT(GVL_CSP_NEW.fMoveTimeSec / cTaskSec);
            fDuration := UDINT_TO_LREAL(nTotalTicks) * cTaskSec;
            tLatchedPause := GVL_CSP_NEW.tPauseTime;
            nMotionTicks := UDINT#0;
            fElapsed := 0.0;
            bTurnCounted := FALSE;
            nPhase := USINT#1;
        END_IF;
    END_IF;
END_IF;

(* Also catch faults discovered in the trajectory/arrival logic this cycle. *)
IF bArmed AND ((nStopMask AND UINT#639) <> UINT#0) THEN
    GVL_CSP_NEW.bStopLatched := TRUE;
    GVL_CSP_NEW.bEnable := FALSE;
    GVL_CSP_NEW.bRun := FALSE;
    GVL_CSP_NEW.bReady := FALSE;
    GVL_CSP_NEW.qControlWord := UINT#0;
    GVL_CSP_NEW.fCommandRpm := 0.0;
    nPhase := USINT#0;
    IF GVL_CSP_NEW.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
END_IF;
IF bArmed AND (nStopMask <> UINT#0) THEN
    IF GVL_CSP_NEW.nStopCount < UDINT#4294967295 THEN
        GVL_CSP_NEW.nStopCount := GVL_CSP_NEW.nStopCount + UDINT#1;
    END_IF;
    IF NOT GVL_CSP_NEW.bDiagValid OR
       (((nStopMask AND UINT#639) <> UINT#0) AND
        ((GVL_CSP_NEW.nStopReason AND UINT#639) = UINT#0)) THEN
        GVL_CSP_NEW.nStopReason := nStopMask;
        GVL_CSP_NEW.diagWcState := bWc;
        GVL_CSP_NEW.diagEthercatState := nEcState;
        GVL_CSP_NEW.diagStatusWord := nStatus;
        GVL_CSP_NEW.diagModeDisplay := nMode;
        GVL_CSP_NEW.diagActualPosition := nActualPosition;
        GVL_CSP_NEW.diagActualVelocity := nActualVelocity;
        GVL_CSP_NEW.diagControlWord := nOldControlWord;
        GVL_CSP_NEW.diagIssuedControlWord := GVL_CSP_NEW.qControlWord;
        GVL_CSP_NEW.diagTargetPosition := nOldTargetPosition;
        GVL_CSP_NEW.diagPhase := nOldPhase;
        GVL_CSP_NEW.diagEnableRequest := bReqEnable;
        GVL_CSP_NEW.diagRunRequest := bReqRun;
        GVL_CSP_NEW.diagCompletedTurns := GVL_CSP_NEW.nCompletedTurns;
        GVL_CSP_NEW.diagCycle := GVL_CSP_NEW.nCycleCount;
        GVL_CSP_NEW.diagMasterDiagValid := bMasterDiagLinked;
        GVL_CSP_NEW.diagMasterDevState := nMasterDevState;
        GVL_CSP_NEW.diagMasterFrameWcState := nMasterFrameWcState;
        GVL_CSP_NEW.diagMasterFrameState := nMasterFrameState;
        GVL_CSP_NEW.diagFrameMissing := bMasterDiagLinked AND
            ((nMasterFrameWcState AND UINT#16#8000) <> UINT#0);
        GVL_CSP_NEW.diagCommandWcError := bMasterDiagLinked AND
            ((nMasterFrameWcState AND UINT#16#7FFF) <> UINT#0);
        GVL_CSP_NEW.diagFrameNotSent := bMasterDiagLinked AND
            ((nMasterFrameState AND UINT#16#8000) <> UINT#0);
        (* Valid flag is written last; live fields cannot overwrite the snapshot. *)
        GVL_CSP_NEW.bDiagValid := TRUE;
    END_IF;
END_IF;
GVL_CSP_NEW.qTargetPosition := LREAL_TO_DINT(fHold);
GVL_CSP_NEW.fFollowingErrorCounts :=
    DINT_TO_LREAL(GVL_CSP_NEW.qTargetPosition) -
    DINT_TO_LREAL(nActualPosition);
GVL_CSP_NEW.nPhase := nPhase;
GVL_CSP_NEW.bMoving := GVL_CSP_NEW.bReady AND (nPhase = USINT#1);
GVL_CSP_NEW.bWaitingToSettle := GVL_CSP_NEW.bReady AND (nPhase = USINT#2);
GVL_CSP_NEW.bPausing := GVL_CSP_NEW.bReady AND (nPhase = USINT#3);
GVL_CSP_NEW.tPauseElapsed := fbPause.ET;
bPrevEnable := GVL_CSP_NEW.bEnable;
bPrevRun := GVL_CSP_NEW.bRun;
bPrevReady := GVL_CSP_NEW.bReady;
