PROGRAM MAIN
VAR
    nDriveState : UINT;
    nPhase      : USINT;
    bPlanMove   : BOOL;
    bTurnCounted : BOOL;
    fHold       : LREAL;
    fStart      : LREAL;
    fDelta      : LREAL;
    fEnd        : LREAL;
    fDuration   : LREAL;
    fElapsed    : LREAL;
    fU          : LREAL;
    fShape      : LREAL;
    fbSettled   : TON;
    fbPause     : TON;
    fbArrival   : TON;
END_VAR

VAR CONSTANT
    // Match PlcTask and the associated EtherCAT update to 2 ms.
    cTaskSec          : LREAL := 0.002;
    cMinMoveSec       : LREAL := 2.0;
    cMaxMoveSec       : LREAL := 60.0;
    cMaxCountsPerRev  : DINT := 1000000;
    cPositionLimit    : LREAL := 2000000000.0;
    cToleranceCounts : LREAL := 2.0;
    cSettleTime       : TIME := T#100ms;
    cPauseTime        : TIME := T#1s;
    cArrivalTimeout   : TIME := T#5s;
END_VAR
