PROGRAM MAIN
VAR
    nDriveState : UINT;
    bMoving     : BOOL;
    fHold       : LREAL;
    fStart      : LREAL;
    fDelta      : LREAL;
    fEnd        : LREAL;
    fElapsed    : LREAL;
    fDuration   : LREAL;
    fU          : LREAL;
    fShape      : LREAL;
END_VAR
VAR CONSTANT
    // PlcTask and the corresponding EtherCAT update must both be 2 ms.
    cTaskSec : LREAL := 0.002;
END_VAR
