{attribute 'qualified_only'}
VAR_GLOBAL
    (* Outputs: link to this slave only. Do not link an NC axis. *)
    qControlWord AT %Q* : UINT;
    qTargetPosition AT %Q* : DINT;
    qTargetVelocity AT %Q* : DINT;
    qMode AT %Q* : SINT;
    (* Inputs: WcState and State must belong to this slave. *)
    iStatusWord AT %I* : UINT;
    iActualPosition AT %I* : DINT;
    iActualVelocity AT %I* : DINT;
    iModeDisplay AT %I* : SINT;
    iWcState AT %I* : BOOL;
    iEthercatState AT %I* : UINT;

    (* Write values, never Force values. Start Enable, then wait Ready, then Run. *)
    bEnable : BOOL := FALSE;
    bRun : BOOL := FALSE;
    bResetFault : BOOL := FALSE;
    bResetDiag : BOOL := FALSE;
    nCountsPerRev : DINT := 1000;
    fMoveTimeSec : LREAL := 2.0;
    (* Positive-only turns. Must equal MCU sxifen; change only while disabled. *)
    fPeakRpm : LREAL;
    fPeakAcceleration : LREAL;

    bBusOk : BOOL;
    bFault : BOOL;
    bReady : BOOL;
    bEstimatedAtTarget : BOOL;
    bDriveAtTarget : BOOL;
    bMoving : BOOL;
    bWaitingToSettle : BOOL;
    bPausing : BOOL;
    bConfigError : BOOL;
    bArrivalTimeout : BOOL;
    bEnableTimeout : BOOL;
    fFollowingErrorCounts : LREAL;
    bPositionLimit : BOOL;
    bStopLatched : BOOL;
    nPhase : USINT;
    nCompletedTurns : UDINT;
    fCommandRpm : LREAL;
    tPauseElapsed : TIME;

    (* First fault replaces a prior normal-stop event. Then preserved. Not RETAIN. *)
    bDiagValid : BOOL;
    nStopReason : UINT;
    nStopCount : UDINT;
    nCycleCount : UDINT;
    nBadWcCycles : UDINT;
    nNonOpCycles : UDINT;
    nFaultCycles : UDINT;
    diagWcState : BOOL;
    diagEthercatState : UINT;
    diagStatusWord : UINT;
    diagModeDisplay : SINT;
    diagActualPosition : DINT;
    diagActualVelocity : DINT;
    diagControlWord : UINT;
    diagTargetPosition : DINT;
    diagPhase : USINT;
    diagEnableRequest : BOOL;
    diagRunRequest : BOOL;
    diagCompletedTurns : UDINT;
    diagCycle : UDINT;
END_VAR
