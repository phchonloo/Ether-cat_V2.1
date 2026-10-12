PROGRAM MAIN
VAR
    bWc : BOOL;
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
    fbSettled : TON;
    fbPause : TON;
    fbArrival : TON;
END_VAR
VAR CONSTANT
    cTaskSec : LREAL := 0.002;
    cMinMoveSec : LREAL := 1.2;
    cMaxMoveSec : LREAL := 60.0;
    cMaxCountsPerRev : DINT := 1000000;
    cPositionLimit : LREAL := 2000000000.0;
    cToleranceCounts : LREAL := 2.0;
    cSettleTime : TIME := T#100ms;
    cPauseTime : TIME := T#1s;
    cArrivalTimeout : TIME := T#5s;
END_VAR
