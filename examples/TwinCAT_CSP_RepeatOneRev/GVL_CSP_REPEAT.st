{attribute 'qualified_only'}
VAR_GLOBAL
    // PLC outputs -> slave RxPDO. No NC or other program may also write them.
    qControlWord       AT %Q* : UINT;
    qTargetPosition    AT %Q* : DINT;
    qTargetVelocity    AT %Q* : DINT;
    qMode              AT %Q* : SINT;

    // Slave feedback and diagnostics -> PLC inputs.
    iStatusWord        AT %I* : UINT;
    iActualPosition    AT %I* : DINT;
    iActualVelocity    AT %I* : DINT;
    iModeDisplay       AT %I* : SINT;
    iWcState           AT %I* : BOOL;
    iEthercatState     AT %I* : UINT;

    // Use Write values, never Force values.
    bEnable            : BOOL := FALSE;
    bRun               : BOOL := FALSE;
    bResetFault        : BOOL := FALSE;
    bResetSequence     : BOOL := FALSE;

    // Parameters are latched when starting each revolution.
    nCountsPerRev      : DINT := 1000;
    fMoveTimeSec       : LREAL := 2.0;

    // Monitor only. Completion uses estimated, not encoder, feedback.
    bBusOk             : BOOL;
    bFault             : BOOL;
    bReady             : BOOL;
    bEstimatedAtTarget : BOOL;
    bMoving            : BOOL;
    bWaitingToSettle   : BOOL;
    bPausing           : BOOL;
    bConfigError       : BOOL;
    bArrivalTimeout    : BOOL;
    bPositionLimit     : BOOL;
    nPhase             : USINT;
    nCompletedTurns    : UDINT;
    fCommandRpm        : LREAL;
    tPauseElapsed      : TIME;
END_VAR
