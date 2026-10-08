{attribute 'qualified_only'}
VAR_GLOBAL
    // PLC outputs -> slave RxPDO
    qControlWord       AT %Q* : UINT;
    qTargetPosition    AT %Q* : DINT;
    qTargetVelocity    AT %Q* : DINT;
    qMode              AT %Q* : SINT;

    // Slave TxPDO -> PLC inputs
    iStatusWord        AT %I* : UINT;
    iActualPosition    AT %I* : DINT;
    iActualVelocity    AT %I* : DINT;
    iModeDisplay       AT %I* : SINT;
    iWcState           AT %I* : BOOL;
    iEthercatState     AT %I* : UINT;

    // Use PLC > Write values to operate these variables.
    bEnable                  : BOOL := FALSE;
    bStartMove               : BOOL := FALSE;
    bResetFault              : BOOL := FALSE;
    nMoveCounts              : DINT := 1000;
    fMoveTimeSec             : LREAL := 4.0;

    // Monitor only; MAIN calculates these variables.
    bBusOk                   : BOOL;
    bFault                   : BOOL;
    bReady                   : BOOL;
    bBusy                    : BOOL;
    bTrajectoryDone          : BOOL;
    bEstimatedAtTarget       : BOOL;
    bRequestRejected         : BOOL;
END_VAR
