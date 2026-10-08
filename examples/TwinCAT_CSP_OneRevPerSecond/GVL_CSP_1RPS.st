{attribute 'qualified_only'}
VAR_GLOBAL
    // PLC outputs -> slave RxPDO. Do not also link these outputs to NC.
    qControlWord       AT %Q* : UINT;
    qTargetPosition    AT %Q* : DINT;
    qTargetVelocity    AT %Q* : DINT;
    qMode              AT %Q* : SINT;

    // Slave TxPDO and slave diagnostics -> PLC inputs.
    iStatusWord        AT %I* : UINT;
    iActualPosition    AT %I* : DINT;
    iActualVelocity    AT %I* : DINT;
    iModeDisplay       AT %I* : SINT;
    iWcState           AT %I* : BOOL;
    iEthercatState     AT %I* : UINT;

    // Operate with PLC > Write values, never Force values.
    bEnable            : BOOL := FALSE;
    bRun               : BOOL := FALSE;
    bResetFault        : BOOL := FALSE;

    // Monitor only. Command-speed flags do not measure the motor shaft.
    bBusOk                    : BOOL;
    bFault                    : BOOL;
    bReady                    : BOOL;
    bTrajectoryMoving         : BOOL;
    bAtOneRevPerSecond         : BOOL;
    bEstimatedStopped         : BOOL;
    bPositionLimit            : BOOL;
    fCommandRpm               : LREAL;
    fIncrementCounts          : LREAL;
    nOneSecondTargetDelta     : DINT;
    bOneSecondSampleValid     : BOOL;
END_VAR
