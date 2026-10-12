PROGRAM MAIN
VAR
    bWc : BOOL;
    nEcState : UINT;
    nStatus : UINT;
    nErrorCode : UINT;
    nDigitalInputs : UDINT;
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
    fbSettled : TON;
    fbPause : TON;
    fbArrival : TON;
END_VAR
VAR CONSTANT
    cTaskSec : LREAL := 0.002;
    cMinMoveSec : LREAL := 2.0;
    cMaxMoveSec : LREAL := 60.0;
    cMaxCountsPerRev : DINT := 102400;
    cPositionLimit : LREAL := 2000000000.0;
    cToleranceCounts : LREAL := 5.0;
    cVelocityTolerance : LREAL := 5.0; (* pulse/s, not rpm. *)
    cSettleTime : TIME := T#100ms;
    cPauseTime : TIME := T#500ms;
    cArrivalTimeout : TIME := T#5s;
END_VAR
