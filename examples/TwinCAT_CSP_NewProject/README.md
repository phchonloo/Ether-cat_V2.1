# 新建 TwinCAT CSP PLC：单圈／同向循环，复制粘贴版

日期：2026-10-14。示例版本：CSP-NEW-1.1。适用对象：当前 FCM4E1553 自研单轴驱动器的 CSP 模式与现有 PDO 布局。

这是独立的新示例目录，不修改旧 PLC 示例、MCU 固件或 ESI，也不是可导入的 TwinCAT 工程。只需手动新建工程，把下文三个代码块放到指定位置。无需导入旧工程或重复创建 MAIN。

默认动作：从当前反馈位置起，同向移动一圈（1000 计数），轨迹用时 2 秒；到位稳定确认 100 ms 后，再暂停 500 ms，随后下一圈。初始使能、运行均为 FALSE，不会上电自动转动。

重要边界：

- 当前网卡在 TwinCAT 中属于 “for demo use only”，新代码不能改变它的实时能力。通信异常仍会锁存停机；不保证靠换 PLC 代码消除随机停转。
- 本例直接写 PDO，不使用 NC、Motion 或 Tc2_MC2。同一输出不能再由其他 PLC 程序或 NC 轴控制。
- 当前自研驱动反馈是内部相位／开环计数，不是编码器真实转子位置。本例的“到位”不等同于机械精度合格。
- PLC 停机逻辑不是安全功能、STO 或急停硬件。先用空载、可安全停止的工况验证，不能用它替代机械安全措施。

## 1. 新建工程

1. 保存旧工程与本次故障截图，确保电机已停止、旧工程不再请求使能。
2. Visual Studio / TwinCAT XAE：新建 **TwinCAT XAE Project**，名称用 **CSP_New_Test**，选择一个新的保存目录。不要选择导入已有工程。
3. 在项目树 **PLC** 上右键，选择 **Add New Item… → Standard PLC Project**，命名 **PLC_CSP_NEW**。
4. 默认应已有 **POUs → MAIN**，语言选择 **Structured Text (ST)**。已有 MAIN 就直接编辑，不再 Add 一个 MAIN。
5. 在 **GVLs** 上右键 **Add → Global Variable List**，名称必须是 **GVL_CSP_NEW**。
6. 确认 **References** 包含 **Tc2_Standard**，用于四个 TON 定时器；不需要 Tc2_MC2。
7. **SYSTEM → Tasks → PlcTask → Task**：设置实际周期为 **2.000 ms**。若基础 Tick 是 1 ms，Cycle ticks 设为 2；以界面显示的 2.000 ms 为准。
8. 确认 MAIN 在该 PlcTask 中每周期调用一次，不能在另一个程序中重复调用。

## 2. GVL_CSP_NEW：全局变量区

双击 **GVLs → GVL_CSP_NEW**，把默认内容全部替换为下面代码。整个块只放在 GVL 中，不能放进 MAIN 的程序区。

```iecst
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

    (* Optional MASTER diagnostics. Unlinked values are NOT valid evidence. *)
    iMasterDevState AT %I* : UINT;
    iMasterFrameWcState AT %I* : UINT;
    iMasterFrameState AT %I* : UINT;
    bMasterDiagLinked : BOOL := FALSE;

    (* Write values, never Force values. Start Enable, then wait Ready, then Run. *)
    bEnable : BOOL := FALSE;
    bRun : BOOL := FALSE;
    bRepeat : BOOL := TRUE; (* FALSE: one turn per bRun command. *)
    bResetFault : BOOL := FALSE;
    bResetDiag : BOOL := FALSE;
    nCountsPerRev : DINT := 1000;
    fMoveTimeSec : LREAL := 2.0;
    tPauseTime : TIME := T#500ms;
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
    diagMasterDiagValid : BOOL;
    diagMasterDevState : UINT;
    diagMasterFrameWcState : UINT;
    diagMasterFrameState : UINT;
    diagFrameMissing : BOOL;
    diagCommandWcError : BOOL;
    diagFrameNotSent : BOOL;
    nFrameMissingCycles : UDINT;
    nCommandWcErrorCycles : UDINT;
    (* Diagnostic only: these NEVER debounce or bypass WcState protection. *)
    nWcBadStreak : UDINT;
    nWcBadStreakMax : UDINT;
    nWcEpisodeCount : UDINT;
    nLastWcEpisodeCycles : UDINT;
    (* diagControlWord is BEFORE protection; this is what PLC issued AFTER. *)
    diagIssuedControlWord : UINT;
END_VAR
```

## 3. MAIN：上半部分声明区

双击 **POUs → MAIN**。在上方的声明区，把默认 `PROGRAM MAIN / VAR / END_VAR` 全部替换成下面内容。不要在原有 VAR 内再嵌套 VAR。

```iecst
PROGRAM MAIN
VAR
    bWc : BOOL;
    bMasterDiagLinked : BOOL;
    nMasterDevState : UINT;
    nMasterFrameWcState : UINT;
    nMasterFrameState : UINT;
    tLatchedPause : TIME := T#500ms;
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
    nMotionTicks : UDINT;
    nTotalTicks : UDINT;
    nCandidateTarget : DINT;
    fbSettled : TON;
    fbPause : TON;
    fbArrival : TON;
    fbEnable : TON;
END_VAR
VAR CONSTANT
    cTaskSec : LREAL := 0.002;
    cMinMoveSec : LREAL := 1.5;
    cMaxMoveSec : LREAL := 60.0;
    cMaxCountsPerRev : DINT := 40000;
    cPositionLimit : LREAL := 2000000000.0;
    cSettleTime : TIME := T#100ms;
    cArrivalTimeout : TIME := T#5s;
    cEnableTimeout : TIME := T#3s;
    (* Match firmware 0x6081/0x6083/0x6084; never raise only the PLC limits. *)
    cProfileRpm : LREAL := 100.0;
    cProfileAcceleration : LREAL := 300.0;
END_VAR
```

## 4. MAIN：下半部分实现区

下面代码只放在 MAIN 下方的 ST 程序区。这里没有 PROGRAM、VAR 或 END_VAR，不能粘贴到上方声明区。

```iecst
(* Implementation pane ONLY. MAIN must be called once per 2 ms PlcTask. *)
(* Capture inputs and requests before protection code changes them. *)
bWc := GVL_CSP_NEW.iWcState;
nEcState := GVL_CSP_NEW.iEthercatState;
bMasterDiagLinked := GVL_CSP_NEW.bMasterDiagLinked;
nMasterDevState := GVL_CSP_NEW.iMasterDevState;
nMasterFrameWcState := GVL_CSP_NEW.iMasterFrameWcState;
nMasterFrameState := GVL_CSP_NEW.iMasterFrameState;
nStatus := GVL_CSP_NEW.iStatusWord;
nMode := GVL_CSP_NEW.iModeDisplay;
nActualPosition := GVL_CSP_NEW.iActualPosition;
nActualVelocity := GVL_CSP_NEW.iActualVelocity;
bReqEnable := GVL_CSP_NEW.bEnable;
bReqRun := GVL_CSP_NEW.bRun;
nOldControlWord := GVL_CSP_NEW.qControlWord;
nOldTargetPosition := GVL_CSP_NEW.qTargetPosition;
nOldPhase := nPhase;
bArmed := bReqEnable OR bReqRun OR bPrevEnable OR bPrevRun;
nStopMask := UINT#0;
bPlanMove := FALSE;
GVL_CSP_NEW.qMode := SINT#8;
GVL_CSP_NEW.qTargetVelocity := DINT#0;
GVL_CSP_NEW.fCommandRpm := 0.0;
IF GVL_CSP_NEW.nCycleCount < UDINT#4294967295 THEN
    GVL_CSP_NEW.nCycleCount := GVL_CSP_NEW.nCycleCount + UDINT#1;
END_IF;
GVL_CSP_NEW.bBusOk := NOT bWc AND
    ((nEcState AND UINT#16#0FFF) = UINT#8);
GVL_CSP_NEW.bFault := (nStatus AND UINT#16#0008) <> UINT#0;
GVL_CSP_NEW.bConfigError := NOT (
    (GVL_CSP_NEW.nCountsPerRev >= DINT#1) AND
    (GVL_CSP_NEW.nCountsPerRev <= cMaxCountsPerRev) AND
    (GVL_CSP_NEW.fMoveTimeSec >= cMinMoveSec) AND
    (GVL_CSP_NEW.fMoveTimeSec <= cMaxMoveSec) AND
    (GVL_CSP_NEW.tPauseTime >= T#0ms) AND
    (GVL_CSP_NEW.tPauseTime <= T#60s));
IF NOT GVL_CSP_NEW.bConfigError THEN
    GVL_CSP_NEW.fPeakRpm := 112.5 / GVL_CSP_NEW.fMoveTimeSec;
    GVL_CSP_NEW.fPeakAcceleration := 346.410162 /
        (GVL_CSP_NEW.fMoveTimeSec * GVL_CSP_NEW.fMoveTimeSec);
    GVL_CSP_NEW.bConfigError :=
        (GVL_CSP_NEW.fPeakRpm > cProfileRpm) OR
        (GVL_CSP_NEW.fPeakAcceleration > cProfileAcceleration);
END_IF;

(* Reset diagnostics only while disabled, stationary and communicating. *)
IF GVL_CSP_NEW.bResetDiag THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_CSP_NEW.bBusOk AND
       NOT GVL_CSP_NEW.bFault AND (nActualVelocity = DINT#0) THEN
        GVL_CSP_NEW.bDiagValid := FALSE;
        GVL_CSP_NEW.diagMasterDiagValid := FALSE;
        GVL_CSP_NEW.nFrameMissingCycles := UDINT#0;
        GVL_CSP_NEW.nCommandWcErrorCycles := UDINT#0;
        GVL_CSP_NEW.nStopReason := UINT#0;
        GVL_CSP_NEW.nStopCount := UDINT#0;
        GVL_CSP_NEW.nBadWcCycles := UDINT#0;
        GVL_CSP_NEW.nNonOpCycles := UDINT#0;
        GVL_CSP_NEW.nFaultCycles := UDINT#0;
        GVL_CSP_NEW.nWcBadStreak := UDINT#0;
        GVL_CSP_NEW.nWcBadStreakMax := UDINT#0;
        GVL_CSP_NEW.nWcEpisodeCount := UDINT#0;
        GVL_CSP_NEW.nLastWcEpisodeCycles := UDINT#0;
        GVL_CSP_NEW.diagIssuedControlWord := UINT#0;
        GVL_CSP_NEW.bStopLatched := FALSE;
        GVL_CSP_NEW.bArrivalTimeout := FALSE;
        GVL_CSP_NEW.bEnableTimeout := FALSE;
        GVL_CSP_NEW.bPositionLimit := FALSE;
    END_IF;
    GVL_CSP_NEW.bResetDiag := FALSE;
END_IF;

(* Optional frame diagnostics do not weaken or replace the slave WcState guard. *)
IF bMasterDiagLinked THEN
    IF (nMasterFrameWcState AND UINT#16#8000) <> UINT#0 THEN
        IF GVL_CSP_NEW.nFrameMissingCycles < UDINT#4294967295 THEN
            GVL_CSP_NEW.nFrameMissingCycles := GVL_CSP_NEW.nFrameMissingCycles + UDINT#1;
        END_IF;
    END_IF;
    IF (nMasterFrameWcState AND UINT#16#7FFF) <> UINT#0 THEN
        IF GVL_CSP_NEW.nCommandWcErrorCycles < UDINT#4294967295 THEN
            GVL_CSP_NEW.nCommandWcErrorCycles := GVL_CSP_NEW.nCommandWcErrorCycles + UDINT#1;
        END_IF;
    END_IF;
END_IF;

IF bWc THEN
    (* Count consecutive bad PLC scans separately from total bad scans.
     * A single bad scan still trips immediately; do not use stale feedback. *)
    IF GVL_CSP_NEW.nWcBadStreak = UDINT#0 THEN
        IF GVL_CSP_NEW.nWcEpisodeCount < UDINT#4294967295 THEN
            GVL_CSP_NEW.nWcEpisodeCount := GVL_CSP_NEW.nWcEpisodeCount + UDINT#1;
        END_IF;
    END_IF;
    IF GVL_CSP_NEW.nWcBadStreak < UDINT#4294967295 THEN
        GVL_CSP_NEW.nWcBadStreak := GVL_CSP_NEW.nWcBadStreak + UDINT#1;
    END_IF;
    IF GVL_CSP_NEW.nWcBadStreak > GVL_CSP_NEW.nWcBadStreakMax THEN
        GVL_CSP_NEW.nWcBadStreakMax := GVL_CSP_NEW.nWcBadStreak;
    END_IF;
    IF GVL_CSP_NEW.nBadWcCycles < UDINT#4294967295 THEN
        GVL_CSP_NEW.nBadWcCycles := GVL_CSP_NEW.nBadWcCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#1;
ELSE
    IF GVL_CSP_NEW.nWcBadStreak > UDINT#0 THEN
        GVL_CSP_NEW.nLastWcEpisodeCycles := GVL_CSP_NEW.nWcBadStreak;
        GVL_CSP_NEW.nWcBadStreak := UDINT#0;
    END_IF;
END_IF;
IF (nEcState AND UINT#16#0FFF) <> UINT#8 THEN
    IF GVL_CSP_NEW.nNonOpCycles < UDINT#4294967295 THEN
        GVL_CSP_NEW.nNonOpCycles := GVL_CSP_NEW.nNonOpCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#2;
END_IF;
IF GVL_CSP_NEW.bFault THEN
    IF GVL_CSP_NEW.nFaultCycles < UDINT#4294967295 THEN
        GVL_CSP_NEW.nFaultCycles := GVL_CSP_NEW.nFaultCycles + UDINT#1;
    END_IF;
    nStopMask := nStopMask OR UINT#4;
END_IF;
IF bPrevEnable AND NOT bReqEnable THEN
    nStopMask := nStopMask OR UINT#128;
END_IF;
IF bPrevRun AND NOT bReqRun AND bReqEnable THEN
    (* Normal stop request: complete the current turn, no new turn. *)
    nStopMask := nStopMask OR UINT#256;
END_IF;

IF NOT bReqEnable THEN
    GVL_CSP_NEW.bRun := FALSE;
END_IF;

(* CiA402 enable handshake. No automatic fault reset. *)
GVL_CSP_NEW.qControlWord := UINT#0;
GVL_CSP_NEW.bReady := FALSE;
nDriveState := nStatus AND UINT#16#006F;
IF bReqEnable AND GVL_CSP_NEW.bBusOk AND NOT GVL_CSP_NEW.bFault AND
   NOT GVL_CSP_NEW.bStopLatched THEN
    CASE nDriveState OF
        16#0040: GVL_CSP_NEW.qControlWord := UINT#6;
        16#0021: GVL_CSP_NEW.qControlWord := UINT#7;
        16#0023: GVL_CSP_NEW.qControlWord := UINT#15;
        16#0027:
            GVL_CSP_NEW.qControlWord := UINT#15;
            GVL_CSP_NEW.bReady := (nMode = SINT#8) AND
                ((nStatus AND UINT#16#1000) <> UINT#0);
    END_CASE;
END_IF;
IF GVL_CSP_NEW.bResetFault THEN
    IF NOT bReqEnable AND NOT bReqRun AND GVL_CSP_NEW.bBusOk AND
       GVL_CSP_NEW.bFault THEN
        GVL_CSP_NEW.qControlWord := UINT#16#0080;
    END_IF;
    GVL_CSP_NEW.bResetFault := FALSE;
END_IF;

IF GVL_CSP_NEW.bBusOk AND NOT GVL_CSP_NEW.bFault AND
   NOT GVL_CSP_NEW.bStopLatched THEN
    IF bReqEnable AND NOT GVL_CSP_NEW.bReady AND
       bPrevReady THEN
        nStopMask := nStopMask OR UINT#8;
    END_IF;
END_IF;
IF bArmed AND ((nStopMask AND UINT#639) <> UINT#0) THEN
    GVL_CSP_NEW.bStopLatched := TRUE;
END_IF;
IF GVL_CSP_NEW.bStopLatched THEN
    GVL_CSP_NEW.bEnable := FALSE;
    GVL_CSP_NEW.bRun := FALSE;
    GVL_CSP_NEW.bReady := FALSE;
    IF GVL_CSP_NEW.qControlWord <> UINT#16#0080 THEN
        GVL_CSP_NEW.qControlWord := UINT#0;
    END_IF;
END_IF;

(* On first Ready, seed the target from current feedback, never from zero. *)
IF NOT GVL_CSP_NEW.bReady OR NOT bPrevReady THEN
    IF GVL_CSP_NEW.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
    nPhase := USINT#0;
    fElapsed := 0.0;
    nMotionTicks := UDINT#0;
    bTurnCounted := FALSE;
END_IF;
GVL_CSP_NEW.bEstimatedAtTarget := GVL_CSP_NEW.bReady AND
    (nActualVelocity = DINT#0) AND
    (nActualPosition = LREAL_TO_DINT(fHold));
GVL_CSP_NEW.bDriveAtTarget := GVL_CSP_NEW.bEstimatedAtTarget AND
    ((nStatus AND UINT#16#0400) <> UINT#0);

(* Each TON is called exactly once per PLC cycle, including disabled cycles. *)
fbSettled(IN := GVL_CSP_NEW.bReady AND (nPhase = USINT#2) AND
    GVL_CSP_NEW.bDriveAtTarget, PT := cSettleTime);
fbPause(IN := GVL_CSP_NEW.bReady AND GVL_CSP_NEW.bRun AND
    (nPhase = USINT#3) AND GVL_CSP_NEW.bDriveAtTarget, PT := tLatchedPause);
fbArrival(IN := GVL_CSP_NEW.bReady AND (nPhase = USINT#2),
    PT := cArrivalTimeout);
fbEnable(IN := bReqEnable AND GVL_CSP_NEW.bBusOk AND
    NOT GVL_CSP_NEW.bFault AND NOT GVL_CSP_NEW.bReady AND
    NOT GVL_CSP_NEW.bStopLatched, PT := cEnableTimeout);
IF fbEnable.Q THEN
    GVL_CSP_NEW.bEnableTimeout := TRUE;
    nStopMask := nStopMask OR UINT#512;
END_IF;

IF GVL_CSP_NEW.bReady THEN
    CASE nPhase OF
        0:
            bPlanMove := GVL_CSP_NEW.bRun AND GVL_CSP_NEW.bDriveAtTarget;
        1:
            IF nMotionTicks < nTotalTicks THEN
                nMotionTicks := nMotionTicks + UDINT#1;
            END_IF;
            fElapsed := UDINT_TO_LREAL(nMotionTicks) * cTaskSec;
            fU := UDINT_TO_LREAL(nMotionTicks) / UDINT_TO_LREAL(nTotalTicks);
            (* Quintic: position, speed and acceleration join continuously.
             * Integer ticks remove accumulated floating-point time drift. *)
            fShape := fU * fU * fU * (10.0 + fU * (-15.0 + 6.0 * fU));
            IF fShape < 0.0 THEN fShape := 0.0; END_IF;
            IF fShape > 1.0 THEN fShape := 1.0; END_IF;
            nCandidateTarget := LREAL_TO_DINT(fStart + fDelta * fShape);
            (* Never reverse a positive turn due to rounding or feedback reseeding. *)
            IF nCandidateTarget < GVL_CSP_NEW.qTargetPosition THEN
                nCandidateTarget := GVL_CSP_NEW.qTargetPosition;
            END_IF;
            IF nCandidateTarget > LREAL_TO_DINT(fEnd) THEN
                nCandidateTarget := LREAL_TO_DINT(fEnd);
            END_IF;
            fHold := DINT_TO_LREAL(nCandidateTarget);
            GVL_CSP_NEW.fCommandRpm :=
                1800.0 * fU * fU * (1.0 - fU) * (1.0 - fU) / fDuration;
            IF nMotionTicks >= nTotalTicks THEN
                fHold := fEnd;
                GVL_CSP_NEW.fCommandRpm := 0.0;
                nPhase := USINT#2;
            END_IF;
        2:
            IF fbSettled.Q THEN
                IF NOT bTurnCounted THEN
                    IF GVL_CSP_NEW.nCompletedTurns < UDINT#4294967295 THEN
                        GVL_CSP_NEW.nCompletedTurns :=
                            GVL_CSP_NEW.nCompletedTurns + UDINT#1;
                    END_IF;
                    bTurnCounted := TRUE;
                END_IF;
                IF NOT GVL_CSP_NEW.bRepeat THEN
                    GVL_CSP_NEW.bRun := FALSE;
                END_IF;
                IF GVL_CSP_NEW.bRun THEN
                    nPhase := USINT#3;
                ELSE
                    nPhase := USINT#0;
                END_IF;
            ELSIF fbArrival.Q THEN
                GVL_CSP_NEW.bArrivalTimeout := TRUE;
                nStopMask := nStopMask OR UINT#16;
            END_IF;
        3:
            IF NOT GVL_CSP_NEW.bRun THEN
                nPhase := USINT#0;
            ELSIF NOT GVL_CSP_NEW.bDriveAtTarget THEN
                nPhase := USINT#2;
            ELSIF fbPause.Q THEN
                bPlanMove := TRUE;
            END_IF;
    END_CASE;
END_IF;

IF bPlanMove THEN
    IF GVL_CSP_NEW.bConfigError THEN
        nStopMask := nStopMask OR UINT#64;
    ELSE
        (* Latch parameters for this turn. Check bounds before DINT conversion. *)
        fStart := fHold;
        fDelta := DINT_TO_LREAL(GVL_CSP_NEW.nCountsPerRev);
        fEnd := fStart + fDelta;
        IF (ABS(fStart) > cPositionLimit) OR (ABS(fEnd) > cPositionLimit) THEN
            GVL_CSP_NEW.bPositionLimit := TRUE;
            nStopMask := nStopMask OR UINT#32;
        ELSE
            nTotalTicks := LREAL_TO_UDINT(GVL_CSP_NEW.fMoveTimeSec / cTaskSec);
            fDuration := UDINT_TO_LREAL(nTotalTicks) * cTaskSec;
            tLatchedPause := GVL_CSP_NEW.tPauseTime;
            nMotionTicks := UDINT#0;
            fElapsed := 0.0;
            bTurnCounted := FALSE;
            nPhase := USINT#1;
        END_IF;
    END_IF;
END_IF;

(* Also catch faults discovered in the trajectory/arrival logic this cycle. *)
IF bArmed AND ((nStopMask AND UINT#639) <> UINT#0) THEN
    GVL_CSP_NEW.bStopLatched := TRUE;
    GVL_CSP_NEW.bEnable := FALSE;
    GVL_CSP_NEW.bRun := FALSE;
    GVL_CSP_NEW.bReady := FALSE;
    GVL_CSP_NEW.qControlWord := UINT#0;
    GVL_CSP_NEW.fCommandRpm := 0.0;
    nPhase := USINT#0;
    IF GVL_CSP_NEW.bBusOk THEN
        fHold := DINT_TO_LREAL(nActualPosition);
    END_IF;
END_IF;
IF bArmed AND (nStopMask <> UINT#0) THEN
    IF GVL_CSP_NEW.nStopCount < UDINT#4294967295 THEN
        GVL_CSP_NEW.nStopCount := GVL_CSP_NEW.nStopCount + UDINT#1;
    END_IF;
    IF NOT GVL_CSP_NEW.bDiagValid OR
       (((nStopMask AND UINT#639) <> UINT#0) AND
        ((GVL_CSP_NEW.nStopReason AND UINT#639) = UINT#0)) THEN
        GVL_CSP_NEW.nStopReason := nStopMask;
        GVL_CSP_NEW.diagWcState := bWc;
        GVL_CSP_NEW.diagEthercatState := nEcState;
        GVL_CSP_NEW.diagStatusWord := nStatus;
        GVL_CSP_NEW.diagModeDisplay := nMode;
        GVL_CSP_NEW.diagActualPosition := nActualPosition;
        GVL_CSP_NEW.diagActualVelocity := nActualVelocity;
        GVL_CSP_NEW.diagControlWord := nOldControlWord;
        GVL_CSP_NEW.diagIssuedControlWord := GVL_CSP_NEW.qControlWord;
        GVL_CSP_NEW.diagTargetPosition := nOldTargetPosition;
        GVL_CSP_NEW.diagPhase := nOldPhase;
        GVL_CSP_NEW.diagEnableRequest := bReqEnable;
        GVL_CSP_NEW.diagRunRequest := bReqRun;
        GVL_CSP_NEW.diagCompletedTurns := GVL_CSP_NEW.nCompletedTurns;
        GVL_CSP_NEW.diagCycle := GVL_CSP_NEW.nCycleCount;
        GVL_CSP_NEW.diagMasterDiagValid := bMasterDiagLinked;
        GVL_CSP_NEW.diagMasterDevState := nMasterDevState;
        GVL_CSP_NEW.diagMasterFrameWcState := nMasterFrameWcState;
        GVL_CSP_NEW.diagMasterFrameState := nMasterFrameState;
        GVL_CSP_NEW.diagFrameMissing := bMasterDiagLinked AND
            ((nMasterFrameWcState AND UINT#16#8000) <> UINT#0);
        GVL_CSP_NEW.diagCommandWcError := bMasterDiagLinked AND
            ((nMasterFrameWcState AND UINT#16#7FFF) <> UINT#0);
        GVL_CSP_NEW.diagFrameNotSent := bMasterDiagLinked AND
            ((nMasterFrameState AND UINT#16#8000) <> UINT#0);
        (* Valid flag is written last; live fields cannot overwrite the snapshot. *)
        GVL_CSP_NEW.bDiagValid := TRUE;
    END_IF;
END_IF;
GVL_CSP_NEW.qTargetPosition := LREAL_TO_DINT(fHold);
GVL_CSP_NEW.fFollowingErrorCounts :=
    DINT_TO_LREAL(GVL_CSP_NEW.qTargetPosition) -
    DINT_TO_LREAL(nActualPosition);
GVL_CSP_NEW.nPhase := nPhase;
GVL_CSP_NEW.bMoving := GVL_CSP_NEW.bReady AND (nPhase = USINT#1);
GVL_CSP_NEW.bWaitingToSettle := GVL_CSP_NEW.bReady AND (nPhase = USINT#2);
GVL_CSP_NEW.bPausing := GVL_CSP_NEW.bReady AND (nPhase = USINT#3);
GVL_CSP_NEW.tPauseElapsed := fbPause.ET;
bPrevEnable := GVL_CSP_NEW.bEnable;
bPrevRun := GVL_CSP_NEW.bRun;
bPrevReady := GVL_CSP_NEW.bReady;
```

## 5. 编译与 PDO 绑定

先执行 Build，生成 PLC 实例和过程映像，然后绑定变量。若出现大量 “Unknown type: GVL… / NOT(…)”，优先检查是否把实现代码放错到声明区，以及 GVL 名称是否准确。

在新 TwinCAT 项目中配置现有 EtherCAT 从站，可扫描添加；若询问自动添加 NC 轴，本例不需要。使用与当前固件匹配的 ESI。

### 5.1 必须绑定的十个变量

| GVL_CSP_NEW 变量 | 数据类型 | 从站节点／对象 | 数据方向 |
| --- | --- | --- | --- |
| qControlWord | UINT | Outputs → Control Word，0x6040 | PLC → 从站 |
| qTargetPosition | DINT | Outputs → TargetPosition，0x607A | PLC → 从站 |
| qTargetVelocity | DINT | Outputs → TargetVelocity，0x60FF | PLC → 从站，CSP 中固定为 0，不用它调速 |
| qMode | SINT | Outputs → ModeOfOperation，0x6060 | PLC → 从站，固定 8 |
| iStatusWord | UINT | Inputs → Status Word，0x6041 | 从站 → PLC |
| iActualPosition | DINT | Inputs → ActualPosition，0x6064 | 从站 → PLC |
| iActualVelocity | DINT | Inputs → ActualVelocity，0x606C | 从站 → PLC |
| iModeDisplay | SINT | Inputs → ModeOfOperationDisplay，0x6061 | 从站 → PLC |
| iWcState | BOOL | 本驱动器 → WcState → WcState | 主站诊断 → PLC；FALSE 有效，TRUE 无效 |
| iEthercatState | UINT | 本驱动器 → InfoData → State | 主站诊断 → PLC；正常 OP 为 8 |

树形名称可能有空格或缩写，以对象索引和数据类型为准。不要把 InputToggle 当 WcState，不要把主站 DevState 当从站 State。Pad/填充字节不用绑定。

沿用当前 ESI 的同步配置；使用 DC 时，PLC 周期、Sync Unit Cycle 与 SYNC0 周期均为 2 ms / 2000 µs。新代码不要求修改 ESI 或 MCU 固件。

### 5.2 可选：主站帧级首错诊断

这三项不属于从站 PDO，不需要修改 XML，也不影响前述十项的基本运行。它们用于定位之前的随机停转。

| GVL_CSP_NEW 变量 | 类型 | 建议绑定的主站节点 |
| --- | --- | --- |
| iMasterDevState | UINT | Device 3 (EtherCAT) → Inputs → DevState |
| iMasterFrameWcState | UINT | Device 3 (EtherCAT) → Inputs → Frm0WcState |
| iMasterFrameState | UINT | Device 3 (EtherCAT) → Inputs → Frm0State |

实际主站名称可能不是 Device 3；帧编号可能不是 0，必须选择包含该驱动器过程数据命令的帧。可在主站 EtherCAT 页查看 cyclic frame / command 分配。

确认三项已正确绑定并随 PlcTask 输入更新后，才把 `bMasterDiagLinked` 写为 TRUE。默认 FALSE：代码不把未绑定的零值当成有效诊断证据，基本通信停机保护仍然有效。

若树中没有这些变量，先保持 FALSE，不要绑到名字相近的输出控制字，也不要为了绑定修改 ESI。可另行检查主站诊断变量的显示设置。

首错快照中：

- `diagMasterDiagValid=TRUE` 表示采集时用户已声明正确绑定；程序不能自行验证接线／映射是否正确。
- `diagMasterFrameWcState AND 16#8000` 非零：对应帧未返回。
- `diagMasterFrameWcState AND 16#7FFF` 非零：对应命令工作计数不符，具体低位对应具体命令。
- `diagMasterFrameState AND 16#8000` 非零：对应整帧未发送。
- 这些是 PLC 本扫描周期的输入快照，不是原始抓包，也不能单凭一项归因于网卡或 MCU。

## 6. 启动、停止和手动恢复

所有在线操作都使用 **Write Values / 写入值**，不要 Force / 强制值，不要直接强制 qControlWord 或 qTargetPosition。

### 首次单圈测试

1. 所有参数修改时保持 `bEnable=FALSE`、`bRun=FALSE`，确认机械可安全运动。
2. 激活新配置，登录新 PLC，启动 PlcTask。该操作由你手动执行，本示例生成过程没有激活配置或驱动电机。
3. 确认 `bBusOk=TRUE`、`iEthercatState=8`、`iWcState=FALSE`、`bFault=FALSE`。
4. 写入 `bResetDiag=TRUE` 一次，清除旧 PLC 诊断。该变量会自动回 FALSE。它不清 MCU 首错记录。
5. 写 `bRepeat=FALSE`，保持默认 `nCountsPerRev=1000`、`fMoveTimeSec=2.0`。
6. 写 `bEnable=TRUE`。代码按状态反馈自动执行控制字 6 → 7 → 15；等待 `bReady=TRUE`，此时模式显示应为 8。
7. 写 `bRun=TRUE`。目标从当前反馈位置起平滑增加一圈，不从位置 0 突跳。
8. 完成后 `nCompletedTurns` 增加 1，`bRun` 自动回 FALSE，目标位置保持，驱动仍使能。用轴上标记检查实际是否一圈。

### 连续同向单圈循环

单圈验证通过后，保持驱动就绪，写 `bRepeat=TRUE`，再写 `bRun=TRUE`。每圈完成并确认到位后，暂停再开始下一圈。没有“转到 304 圈／某个固定循环数自动停止”的逻辑。

`nCountsPerRev`、`fMoveTimeSec`、`tPauseTime` 在每圈开始时锁定；运行中编辑不会改变本圈轨迹和本圈暂停时间，下一圈生效。建议仍在停机状态修改。

### 正常停止

- `bRun=FALSE`：完成正在执行的一圈并到位后保持，不再开始下一圈。不是立即急停。
- `bEnable=FALSE`：立即撤销 PLC 使能请求，控制字变为 0；不是已认证的减速停止或急停功能。

### 异常停止后恢复

通信无效、掉 OP、驱动故障、丢失就绪、到位超时、使能超时、参数越界或位置范围越界都会锁存停止，控制字归 0。恢复通信后不会自动重新启动。

1. 先保存 `nStopReason`、`diag…` 首错和 MCU 诊断，不要先清零。
2. 处理故障原因；确保机械安全、实际静止，`bEnable=FALSE`、`bRun=FALSE`。
3. 若驱动仍有 Fault 且通信已恢复，可手动写 `bResetFault=TRUE` 一次；该请求只发一个控制字 128 脉冲，不会自动循环复位。MCU 有 PDI 故障锁存时，还必须满足固件的有效 PDO 和手动恢复条件。
4. 确认 Fault 已清、总线正常、反馈速度为 0，再写 `bResetDiag=TRUE` 一次；不满足条件时不会解除 PLC 锁存。
5. 重新手动写 `bEnable=TRUE`，等 `bReady=TRUE`，最后才写 `bRun=TRUE`。

## 7. 速度、间隔和诊断怎么看

### 常用参数

| 参数 | 默认值 | 如何修改 |
| --- | --- | --- |
| nCountsPerRev | 1000 | 必须与当前固件／电子齿轮的每圈位置计数一致；本例支持 1～40000 的正值 |
| fMoveTimeSec | 2.0 | 单圈目标轨迹用时；减小则更快，本例允许 1.5～60 秒 |
| tPauseTime | T#500ms | 暂停时间；本例允许 0～60 秒 |
| bRepeat | TRUE | FALSE 单圈，TRUE 同向循环 |
| bEnable | FALSE | 手动使能请求 |
| bRun | FALSE | 手动运动请求 |
| bMasterDiagLinked | FALSE | 仅在三个可选主站诊断输入正确绑定后置 TRUE |

本例是五次平滑轨迹，位置、速度和加速度在圈首／圈尾连续衔接为静止状态。调速靠 `fMoveTimeSec`，不靠 `qTargetVelocity`。

| 单圈时间 | 单圈平均速度 | 理论峰值速度 | 理论峰值加速度 |
| --- | --- | --- | --- |
| 2.0 s | 30 rpm | 56.25 rpm | 约 86.60 rpm/s |
| 1.5 s | 40 rpm | 75 rpm | 约 153.96 rpm/s |

峰值速度为 `112.5 / fMoveTimeSec`，峰值加速度为 `346.410162 / fMoveTimeSec²`。这里不是独立可调的梯形加／减速参数；改变单圈时间会同时改变两者。本例速度／加速度上限为 100 rpm／300 rpm/s，须与实际固件限制核对，不要只提高 PLC 常量。

暂停前还有 100 ms 连续到位确认，因此从到位到下一圈的实际静止间隔至少约为 `100ms + tPauseTime`，另受扫描相位和实际跟随过程影响。要改这段稳定确认时间，查看 MAIN 声明区的 `cSettleTime`，不要误把它当单圈运行时间。

本例严格要求位置反馈等于目标、速度为 0 且状态字 bit10 表示到位。适用于当前自研开环计数反馈，不是通用编码器伺服容差控制。到位条件不满足不会跳过等待进入下一圈，而是在超时后停机。

### nStopReason 是位掩码，可同时包含多个原因

| 值 | 含义 |
| --- | --- |
| 1 | 从站 WcState 无效 |
| 2 | 从站不在正常 OP |
| 4 | CiA402 Fault |
| 8 | 运行中丢失就绪／CSP 模式或 follows-command 条件丢失 |
| 16 | 到位等待超时 |
| 32 | 目标位置超出本例 ±2000000000 计数范围 |
| 64 | 单圈参数不合法或超过示例速度／加速度限制 |
| 128 | 人工撤销 bEnable，正常记录，不作为故障锁存 |
| 256 | 人工撤销 bRun，正常记录，不作为故障锁存 |
| 512 | 使能握手超过 3 秒仍未就绪 |

例如值 3 表示 1 和 2 同时存在，不是新的第三种错误。累计 `nBadWcCycles` 不是连续丢帧计数。首个故障快照保留到手动清诊断；此前的正常停止记录不能遮住后续首次故障。

### 1.1 新增：区分短时 WcState 异常与持续异常

| 变量 | 含义 |
| --- | --- |
| nWcBadStreak | 当前连续 WcState=TRUE 的 PLC 扫描数，恢复有效即归零 |
| nWcBadStreakMax | 连续异常扫描数的历史最大值 |
| nWcEpisodeCount | WcState 从有效进入无效的段数，也统计启动时首段无效 |
| nLastWcEpisodeCycles | 最近一段已结束异常的扫描数 |
| diagControlWord | 首错发生、保护处理前的控制字，运行时通常为 15 |
| diagIssuedControlWord | 同次首错中，PLC 保护处理后发出的控制字，WcState 停机应为 0 |

这些是 PLC 扫描统计，不是以太网丢帧数量或抓包结果。2 ms 任务中一段长度 1 表示只观察到一个无效扫描，不能据此断言恰好丢了一帧。统计在电机停止后继续更新，并由符合原有条件的 `bResetDiag` 清零。

单次 WcState 异常仍立即锁存停机；没有新增去抖容忍窗口，也不会通信恢复后自动使能。`diagIssuedControlWord` 只是 PLC 软件输出映像，不能证明从站已经收到，应与 MCU 首次撤使能快照核对。

配套 MCU 的有界 PDO 重触发处理及新时间诊断见 [2026-10-14 修正说明](../../docs/CSP_PDO_BURST_FIX_20261014.md)。该固件修改不改变 PDO 布局或 ESI。

### 相位与完成计数

- nPhase=0：空闲／未就绪。
- nPhase=1：生成本圈目标轨迹。
- nPhase=2：等待实际反馈到位并稳定确认。
- nPhase=3：暂停。
- nCompletedTurns：完成到位稳定确认的圈数，不是简单按经过时间累计。

位置不断累加时，最终可能触及本例位置保护范围；需要另行设计停机重新定零／坐标回绕。本例不在运行中重写坐标，也不自动消除位置保护。

## 8. 验证记录与边界

离线验证脚本：`node examples/TwinCAT_CSP_NewProject/verify_example.js`。它在本仓库内复用原 CSP 示例的受限 ST 回放解释器，并直接读取本目录的新 ST 源码；不是另写一个 JavaScript 运动控制器。复制到 TwinCAT 运行时不需要 JavaScript 或其他示例目录。

验证范围包含握手、正向单圈轨迹、精确目标端点、到位条件、暂停、单圈／循环、参数锁定、停机锁存、手动复位、可选主站帧级快照和未绑定诊断输入的边界。1.1 另覆盖单次异常不自动重启、分段／连续异常统计、计数饱和、人工复位和 README 复制块与 ST 文件一致性。

未使用 TwinCAT 编译器编译这份新 PLC，也未连接真实电机验证。离线通过不等同于实板验证、机械定位精度或商用可靠性通过。用户仍需在新工程内 Build 并检查实际映射和任务周期。

参考：[Beckhoff TON 与 Tc2_Standard](https://infosys.beckhoff.com/content/1033/tcplclib_tc2_standard/74406539.html)、[主站帧与 WcState 诊断](https://infosys.beckhoff.com/content/1033/tcsystemmanager/1089009035.html)、[EtherCAT 实时网卡分类指南](https://download.beckhoff.com/download/document/io/training/TR8100_installation_guide.pdf)。
