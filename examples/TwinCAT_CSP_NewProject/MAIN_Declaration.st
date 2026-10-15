PROGRAM MAIN
VAR
    bWc : BOOL;
    bMasterDiagLinked : BOOL;
    nMasterDevState : UINT;
    nMasterFrameWcState : UINT;
    nMasterFrameState : UINT;
    tLatchedPause : TIME := T#500ms;
    nEcState : UINT;
    nStatus : UINT;
    nMode : SINT;
    nActualPosition : DINT;
    nActualVelocity : DINT;
    bReqEnable : BOOL;
    bReqRun : BOOL;
    bPrevEnable : BOOL;
    bPrevRun : BOOL;
    bPrevReady : BOOL;
    bArmed : BOOL;
    bPlanMove : BOOL;
    bTurnCounted : BOOL;
    nDriveState : UINT;
    nStopMask : UINT;
    nOldControlWord : UINT;
    nOldTargetPosition : DINT;
    nOldPhase : USINT;
    nPhase : USINT;
    fHold : LREAL;
    fStart : LREAL;
    fEnd : LREAL;
    fDelta : LREAL;
    fDuration : LREAL;
    fElapsed : LREAL;
    fU : LREAL;
    fShape : LREAL;
    nMotionTicks : UDINT;
    nTotalTicks : UDINT;
    nCandidateTarget : DINT;
    fbSettled : TON;
    fbPause : TON;
    fbArrival : TON;
    fbEnable : TON;
END_VAR
VAR CONSTANT
    cTaskSec : LREAL := 0.002;
    cMinMoveSec : LREAL := 1.5;
    cMaxMoveSec : LREAL := 60.0;
    cMaxCountsPerRev : DINT := 40000;
    cPositionLimit : LREAL := 2000000000.0;
    cSettleTime : TIME := T#100ms;
    cArrivalTimeout : TIME := T#5s;
    cEnableTimeout : TIME := T#3s;
    (* Match firmware 0x6081/0x6083/0x6084; never raise only the PLC limits. *)
    cProfileRpm : LREAL := 100.0;
    cProfileAcceleration : LREAL := 300.0;
END_VAR
