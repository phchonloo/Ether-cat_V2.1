# 自研驱动器：新版 CSP 单圈循环（2026-10-09）

独立工程名：TwinCAT_CSP_SmoothRepeat。不要导入旧工程，不要把本例叠加到 FastRepeat 上。
本例正转一圈，确认到位并连续稳定 100 ms，再等待 500 ms，重复下一圈。
默认轨迹 2 秒一圈；先空载验证，稳定后可改为 1.5 秒一圈。

## 先说明“过头再反向”

代码中确实存在两类软件风险：旧 CSP 的速度前馈保留了上一个目标的方向；
每次到达离散 PDO 目标时，又可能把仍较大的相位步长直接截为零。
离线 ARM 复现中，旧目标切换产生过反向步长，旧 1.2 秒轨迹的最大步长突变约 69 rpm。
这不是说已经证明实机每次回摆都由同一个原因造成。

固件已改为按精确的目标相位差生成带小数累计的 20 ms 平滑跟随步长，
仍保留加减速限制和到点限幅；旧方向不能继续远离新 CSP 目标。
新版 PLC 使用五次曲线：起止速度和加速度均为零，正转目标严格单调，停车后保持固定目标。
20 ms 跟随会引入正常的相位滞后，故必须等待到位，不能只等轨迹时间结束。

重要限制：6064/606C 仍是已输出电气相位的计数/速度，不是编码器测量。
这能避免软件指令越点，却不能检测机械丢步、负载惯性回摆或转子失步。
真实机械精度需用轴端标记、角度仪或编码器验证；本例不是 STO 或安全运动功能。

## 1. 先更新 MCU，再测试新 PLC

1. 停止测试并禁止使能，确保电机周围无人员、负载不会掉落，有独立硬件急停。
2. 在 Keil 打开工作区下 program/FCM4E1553 Ethercat/MDK-ARM/Project.uvprojx。
3. Rebuild N32G455 目标；确认无编译错误后，按原流程下载并重新上电。
4. 本次修改 EthercatMotorControl.c/.h；之前的 PulseHandle.c、PwmInit.c 相位计数修改也必须包含。
5. 本次没有修改 PDO 布局或 ESI；使用与当前固件匹配的现有 XML。
6. MCU 的 sxifen 默认 1000；它必须等于 PLC 的 nCountsPerRev。不是电机机械步数 200。
   不要运行中修改 sxifen，否则驱动器会保护停机；需要修改时先禁止使能并重启。

不能只换 PLC 不更新固件，否则旧的 CSP 控制逻辑还在。
本次没有替你烧录、启动 TwinCAT 或给电机使能。

## 2. 新建 TwinCAT 工程

1. File → New → Project，选择 TwinCAT XAE Project。
2. 命名 TwinCAT_CSP_SmoothRepeat，选一个新目录。
3. 右键 PLC → Add New Item → Standard PLC Project。
4. PLC 名称填 CSP_Smooth_PLC。不要选择 Add Existing Item。
5. 使用新工程扫描/配置实际 EtherCAT 从站。不要创建或链接 NC 轴。
6. 标准 PLC 工程会自带 MAIN 和 PlcTask，只使用它们，不再建 PRG_CSP_REPEAT。

新建流程依据 [Beckhoff 标准 PLC 工程说明](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2526130571.html)。

## 3. 三份代码分别粘贴在哪里

### GVL：GVL_CSP_SMOOTH.st

右键 CSP_Smooth_PLC Project → GVLs → Add → Global Variable List，
名字必须是 GVL_CSP_SMOOTH。打开它，把默认内容全部替换为 GVL_CSP_SMOOTH.st。
包括 attribute、VAR_GLOBAL 和 END_VAR；不要粘贴到 MAIN 的声明区。

### MAIN 上半部分：MAIN_Declaration.st

双击 POUs → MAIN。上半部分是声明区。
把上半部分全部替换为 MAIN_Declaration.st；只保留一个 PROGRAM MAIN。
VAR CONSTANT 段也要复制。不要把这份文件粘贴到下半部分。

### MAIN 下半部分：MAIN_Implementation.st

把下半部分全部替换为 MAIN_Implementation.st。
这里没有 PROGRAM、VAR 或 END_VAR，只有执行语句。
不需要导入任何现有 PLC 工程，也不需要再创建第二个 MAIN。

确认 PlcTask 下只调用一次 MAIN。Build PLC Project，确认没有错误。
若提示 TON 未知，检查 References 中是否有 Tc2_Standard。

## 4. 设置 2 ms 周期

在 SYSTEM → Tasks → PlcTask（或 PLC 下相应 PlcTask 的实际任务）设置周期显示为 2.000 ms。
如果基础时钟是 1 ms，则 Cycle ticks=2；不要只抄 ticks 数字而不看右侧实际周期。
代码 cTaskSec=0.002 与真实任务周期必须一致，MAIN 每周期执行一次。

先沿用你已测试的 SM/DC 配置，不要为了本例同时更换同步方式。
使用 DC 时，Sync Unit Cycle 和 Sync0 都应匹配实际关联任务的 2000 μs，启用 Sync0；
是否使用 Sync1 应按当前 ESI/固件配置，不能把禁用同步监视当成修复。
周期设置依据 [Beckhoff Task 页说明](https://infosys.beckhoff.com/content/1033/tc3_system/5210416139.html)。

## 5. PDO 变量连接：只允许这一个 PLC 写输出

先 Build PLC，再展开 PLC Instance 的过程映像，将下列变量连接到实际从站。
从站可能叫 Box 1 或 Drive 1；若仍有 Module 1 就展开它，选择真实 PDO，不能连到设备级 Frm0Ctrl/DevCtrl。

| PLC 变量 | 从站变量/对象 | 类型 |
|---|---|---|
| qControlWord | Outputs → Control Word / 6040 | UINT |
| qTargetPosition | Outputs → TargetPosition / 607A | DINT |
| qTargetVelocity | Outputs → TargetVelocity / 60FF | DINT |
| qMode | Outputs → ModeOfOperation / 6060 | SINT |
| iStatusWord | Inputs → Status Word / 6041 | UINT |
| iActualPosition | Inputs → ActualPosition / 6064 | DINT |
| iActualVelocity | Inputs → ActualVelocity / 606C | DINT |
| iModeDisplay | Inputs → ModeOfOperationDisplay / 6061 | SINT |
| iWcState | 本从站 WcState → WcState | BOOL |
| iEthercatState | 本从站 InfoData → State | UINT |

WcState=FALSE 是数据有效，不是故障；InfoData State 正常 OP=8。
本自研固件 606C 单位是 rpm，本例 TargetVelocity 固定为 0，不参与 CSP 轨迹。
不要把商业驱动器的速度单位直接套到本固件。

若选择窗口看不到从站 PDO，检查 Only Unused 过滤及已有链接。
同一个输出不能同时链接 NC、旧 PLC 和新 PLC；解除冲突的旧输出链接，不要删除原工程。
确认激活的是新工程，旧 PLC 不再写这组输出；若以前强制过值，也要解除强制。
在其他窗口手动修改 ControlWord/TargetPosition 会和 PLC 周期写入争用。

## 6. 下载 PLC、使能和开始转动

1. Build 无错误后，按原流程 Activate Configuration、进入 Run。
   这会改变运行配置，操作前确保电机禁止使能。
2. PLC → Login；需要下载时确认下载；然后 Start PLC。
3. 打开 GVL_CSP_SMOOTH 在线监控。确认 bEnable=FALSE、bRun=FALSE。
4. 确认 bBusOk=TRUE、bFault=FALSE、bStopLatched=FALSE。
5. 确认 nCountsPerRev 与 MCU sxifen 相同，默认 1000；fMoveTimeSec 默认 2.0。
6. 双击 bEnable 的 Prepared value 输入 TRUE，执行 PLC → Write values。
7. 等待 bReady=TRUE。程序自动发送控制字 6 → 7 → 15，qMode 始终 8。
   bReady 要求 Operation enabled、模式显示 8，且状态字 bit12 表示跟随命令。
8. bReady 成功后，同样把 bRun 写为 TRUE。
9. 观察目标每圈增加 1000，到点保持不动；停稳 100 ms，再等待 500 ms，开始下一圈。
   nCompletedTurns 每完成一圈只增加一次，nPhase 依次 1→2→3→1。

只用 Write values，不用 Force values，尤其不能强制 bEnable、bRun、控制字和目标位置。
写入只生效一次，程序仍可保护停机；强制则每周期重复覆盖。
见 [Beckhoff 写入与强制的区别](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2527602315.html)。

正常停循环：bRun 写 FALSE，完成当前圈后停止，仍保持使能。
立即禁止驱动：bEnable 写 FALSE，会输出控制字 0；它不是受控减速或安全急停。
涉及人员安全时使用独立硬件急停，不依赖本例软件变量。

## 7. 调速、间隔和到位

禁用且停止时，fMoveTimeSec 写 1.5 可以加快单圈；默认 2.0。
轨迹在每圈开始时锁存参数，运行中改参数只会影响下一圈。优先在停止状态修改。

| 单圈轨迹时间 | 平均转速 | 峰值转速 | 峰值加速度 |
|---|---:|---:|---:|
| 2.0 s | 30 rpm | 56.25 rpm | 86.60 rpm/s |
| 1.5 s | 40 rpm | 75 rpm | 153.96 rpm/s |

不能把 fMoveTimeSec 设为 1.0 来硬提速：会超过当前 100 rpm 限制。
本例允许 1.5～60 秒，且检查曲线峰值是否超过声明区限制。
实际一圈加停顿还包括：驱动器跟随时间、100 ms 停稳确认和 500 ms 等待。

间隔位于 MAIN 声明区 cPauseTime，当前 T#500ms；例如改 T#1s 后重新编译下载。
不要通过改变任务周期来调速。

bEstimatedAtTarget 只表示整数位置相等且返回速度为零，不证明到位。
bDriveAtTarget 还要求 6041 bit10 到位。程序使用后者，再连续确认 100 ms。
停稳后的目标保持不变，不再把目标重新设为当前位置来启动下一圈。

## 8. 自己停了：不用反复断点，先保存首个停机快照

不要先复位或重启。截图 GVL_CSP_SMOOTH 中 nStopReason 以及 diag 开头的变量。
nStopReason 是可组合的位掩码，不一定只等于某一个数。

| 数值 | 含义 |
|---:|---|
| 1 | WcState 曾无效 |
| 2 | 从站曾离开 OP |
| 4 | 状态字出现驱动故障 |
| 8 | 已就绪后丢失使能、模式 8 或跟随标志 |
| 16 | 到轨迹终点后 5 秒仍未报告到位 |
| 32 | 将超出正负 20 亿的测试位置窗口 |
| 64 | 每圈计数、轨迹时间或速度/加速度配置无效 |
| 128 | 外部把 bEnable 写为 FALSE（操作记录，非故障） |
| 256 | 外部把 bRun 写为 FALSE（正常停循环记录） |
| 512 | 使能握手 3 秒未就绪 |

第一个故障可覆盖之前的正常停止记录，之后不会被恢复后的正常值覆盖。
总线或驱动故障会锁存禁止使能，不自动恢复运动。
快照不是 RETAIN，PLC 重新下载/重启后可能丢失，请先保存。

恢复步骤：先确认机械安全，把 bEnable/bRun 都写 FALSE；
若有驱动故障，查 603F/1001 和 MCU ErrNumFlag，排除实际故障后手动 bResetFault=TRUE。
等总线有效、没有驱动故障、速度为零，再写 bResetDiag=TRUE；
确认 bStopLatched=FALSE，再按 Enable → Ready → Run 重启。
故障仍存在时程序不会清除锁存。不要强制变量绕过它。

MCU 建议连续运行监控，不要停断点制造同步超时：

- gEcatPulseDiag：正转测试前重新上电清零；negativeStepCycles 应始终为 0，
  countedPosition/targetPosition 最终相等，lastPhaseStep 最终为 0，atTarget=1。
- mismatchCount 应为 0；否则电机库实际执行与准备步长不一致，不可当作已精确定位。
- blockedAwayCount 表示旧斜坡方向被阻止；纯正转正常测试应为 0。
- maxStepDelta 是电气步长突变，相应 rpm 突变为该值/16。
- gMotorIsrOverrunCount 不应增长；电机中断最坏用时必须小于 62.5 μs，且留有余量。
- s_qspi_timeout_count、gEcatSyncDiag、ErrNumFlag 用于继续区分读写超时、同步丢失和硬件保护。

## 9. 实机验证顺序

1. 空载：默认 2.0 秒，先 10 圈。在轴上做标记，用视频/角度测量确认每圈落点。
2. 同时记录 qTargetPosition、iActualPosition、iStatusWord、iActualVelocity 和 nStopReason。
   暂停阶段目标应完全固定；只有正转测试时 negativeStepCycles 应为零。
3. 再测试 1.5 秒、100 圈、1000 圈，再逐步加入额定负载，最后做长时运行。
4. 若计数严格到位、反向步长计数没有增长，但轴仍过头回摆，
   说明还要查真实机械响应、相电流/电流环、负载惯性、失步或相位标定。
   仅靠本软件反馈无法判定这些项；不能再通过删除同步/故障保护来掩盖。
5. 若又停机，保留首个快照和错误列表后再恢复，避免每次复位把原因抹掉。

## 10. 已完成的离线验证及范围

全量 Keil 构建：0 错误，10 个原有警告（SaveFunc/eeprom_console）。
本次修改的定位控制代码未产生新警告。
22 项新 PLC ST 源码测试、22 项同步源码回放、旧 PLC 28 项回归通过。
ARM 模拟执行的是编译后的相位计数/控制与库 PhaseGet，不是另写的速度定位模型：
验证了单圈/反转精确端点、失去 OP/禁用/故障停止、细分变化保护，
以及 1000/40000 计数、1.5/2 秒平滑轨迹没有非预期反向或相位越点。
新 PLC ST 源码与 ARM 固件还完成了三圈联动测试：等到位后暂停，再发下一圈。

测试器不是 TwinCAT 编译器，不模拟 GPIO、真实 ADC/PWM、电机惯性、实际实时调度或网络。
因此必须在 TwinCAT 中编译并做上述实机测试，不能把离线通过当成机械精度或长时可靠性合格报告。
