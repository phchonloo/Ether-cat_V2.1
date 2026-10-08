PROGRAM MAIN
VAR
    nDriveState   : UINT;
    nLimitedRpm   : DINT;
    fCommandRpm   : LREAL;
    fTargetRpm    : LREAL;
    fRampTarget   : LREAL;
    fRate         : LREAL;
    fStep         : LREAL;
    fDifference   : LREAL;
END_VAR
VAR CONSTANT
    // Match PlcTask and the corresponding EtherCAT update to 2 ms.
    cTaskSec      : LREAL := 0.002;
    // Demonstration limit; current firmware separately clamps +/-1000 rpm.
    cTestLimitRpm : DINT := 100;
END_VAR
