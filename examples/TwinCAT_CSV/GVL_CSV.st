{attribute 'qualified_only'}
VAR_GLOBAL
    qControlWord       AT %Q* : UINT;
    qTargetPosition    AT %Q* : DINT;
    qTargetVelocity    AT %Q* : DINT;
    qMode              AT %Q* : SINT;

    iStatusWord        AT %I* : UINT;
    iActualPosition    AT %I* : DINT;
    iActualVelocity    AT %I* : DINT;
    iModeDisplay       AT %I* : SINT;
    iWcState           AT %I* : BOOL;
    iEthercatState     AT %I* : UINT;

    // Operate with PLC > Write values after bReady becomes TRUE.
    bEnable                  : BOOL := FALSE;
    bResetFault              : BOOL := FALSE;
    nVelocityRequestRpm      : DINT := 0;
    fAccelRpmPerSec          : LREAL := 60.0;
    fDecelRpmPerSec          : LREAL := 60.0;

    // MAIN calculates these diagnostic variables.
    bBusOk                   : BOOL;
    bFault                   : BOOL;
    bReady                   : BOOL;
    bSpeedLimited            : BOOL;
    bAtCommandSpeed          : BOOL;
END_VAR
