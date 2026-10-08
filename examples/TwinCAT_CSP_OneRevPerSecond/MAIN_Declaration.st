PROGRAM MAIN
VAR
    nDriveState        : UINT;
    fPosition          : LREAL;
    fRampRpm           : LREAL;
    nWindowCycles      : UINT;
    nWindowStart       : DINT;
END_VAR

VAR CONSTANT
    // Match these values to the actual PLC task and active motor settings.
    cTaskSec           : LREAL := 0.002;
    cCountsPerRev      : LREAL := 1000.0;
    cRunRpm            : LREAL := 60.0;
    cAccelRpmPerSec    : LREAL := 120.0;
    cDecelRpmPerSec    : LREAL := 120.0;
    cOneSecondCycles   : UINT := 500;

    // Forward-only demo: stop well before the signed DINT upper bound.
    // Never reset the target to zero while the drive remains enabled.
    cPositionStopLimit : LREAL := 2000000000.0;
END_VAR
