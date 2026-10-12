# 新建工程：CSP 快速单圈循环（手动复制粘贴版）

这是全新、独立的测试示例。不要导入旧项目、旧 TcPOU/TcGVL；不要在旧的 CSP_REPEAT 工程上覆盖。只需新建一个 PLC、一个 GVL，使用系统自动生成的 MAIN。

默认动作：正向一圈的指令轨迹约 1.2 秒 → 等待估算位置稳定 100 ms → 保持停止 1 秒 → 再转一圈。实际跟随/停稳可能更久，所以不是每 1.2 秒启动一圈。PLC 和 DC SYNC0 的周期均为 2 ms。

注意：更快的轨迹不等于修好了此前的 DC/偶发停机。此版保留保护，并锁存首次停机条件，帮助找出触发停机的那一周期发生了什么。

## 0. 开始之前

- 先停止旧程序的运动，再进行新工程配置。只连接需要测试的驱动器，先做空载、无危险机构的台架试验；准备独立的急停/断电手段。软件 bEnable 和 bRun 不是安全急停，也不是 STO。
- 驱动器使用当前支持 CSP 的固件和对应 ESI/XML。这个示例不修改 MCU、EEPROM、XML，也不需要重新烧录 MCU。
- 当前固件默认每圈 1000 位置计数，但运行时微步数/位置单位可能被改动。必须确认 nCountsPerRev 与驱动器实际的每圈位置单位相同，否则“加 1000”不一定是机械一圈。可给轴做标记，在空载下确认一圈。
- 当前 6064/606C 是算法估算的位置/速度，没有真实编码器闭环；“估算到位”不能证明没有丢步，nCompletedTurns 是完成指令并满足估算停稳的圈数。
- 当前 MCU 默认 Profile Velocity 为 100 rpm，默认加减速度 300 rpm/s。1.2 秒三次曲线的平均速度 50 rpm、峰值 75 rpm、峰值加速度约 250 rpm/s，留有一定命令余量。若你已改低 0x6081/0x6083/0x6084，则应先恢复合适的限值或增加转动时间；不要仅凭默认值强行加速。负载和电机能力仍需实测。
- 本例只做正向相对一圈循环；不清零实际位置，不每圈跳回位置 0。

## 1. 新建 TwinCAT 工程

1. 打开 TwinCAT XAE / Visual Studio。
2. 选择 File → New → Project。
3. 选择 TwinCAT Projects → TwinCAT XAE Project（不同界面分组名称略有不同）。
4. 名称填 CSP_Fast_Test，选一个新的保存目录，点击 OK。
5. 展开新工程；右键 PLC → Add New Item。
6. 选择 Standard PLC Project，名称填 CSP_Fast_PLC。
7. 展开 CSP_Fast_PLC Project。应有 References、GVLs、POUs 和 PlcTask。
8. 不导入已有工程，不添加第二个运动程序，不连接 NC 轴。

Standard PLC Project 自动生成 MAIN 和 PlcTask，并默认把 MAIN 放入任务。[Beckhoff 新建 PLC 工程说明](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2526130571.html)

## 2. 复制 GVL：一个文件，一处粘贴

1. 在 PLC → CSP_Fast_PLC → CSP_Fast_PLC Project 中，右键 GVLs。
2. Add → Global Variable List，名称填写 GVL_CSP_FAST。
3. 双击新建的 GVL_CSP_FAST。
4. 打开 [GVL_CSP_FAST.st](C:/Users/pc/Desktop/01_FCM4E1553_SQI_82_CTT_V1.4/examples/TwinCAT_CSP_FastRepeat/GVL_CSP_FAST.st)，复制全部内容。
5. 在新建 GVL 编辑器中全选原有空模板，再粘贴全部内容。
6. 保存。文件名必须为 GVL_CSP_FAST，不要命名为 GVL_CSP_REPEAT 或 GVL_CSP_1RPS。

默认参数在这个 GVL 中：nCountsPerRev := 1000、fMoveTimeSec := 1.2。bEnable、bRun 默认为 FALSE，下载不会自动开始运动。

## 3. 复制 MAIN：上半区、下半区分开

1. 展开 POUs，双击系统自动生成的 MAIN (PRG)。
2. MAIN 的语言选择 Structured Text（ST）；如果新模板不是 ST，可以在这个全新的空工程中重新建立一个名为 MAIN 的 ST Program，再确认任务只调用这个 MAIN 一次。
3. 上半区叫“声明区”，可以看到 PROGRAM MAIN、VAR、END_VAR。
4. 打开 [MAIN_Declaration.st](C:/Users/pc/Desktop/01_FCM4E1553_SQI_82_CTT_V1.4/examples/TwinCAT_CSP_FastRepeat/MAIN_Declaration.st)，复制全部内容。
5. 在 MAIN 上半区中全选原有声明并粘贴。PROGRAM MAIN、VAR CONSTANT、三个 TON 都放在上半区。
6. 下半区叫“实现区”，用于 IF、CASE、赋值和定时器调用。
7. 打开 [MAIN_Implementation.st](C:/Users/pc/Desktop/01_FCM4E1553_SQI_82_CTT_V1.4/examples/TwinCAT_CSP_FastRepeat/MAIN_Implementation.st)，复制全部内容。
8. 在 MAIN 下半区中全选原有空实现并粘贴。
9. 保存，然后 Build → Build Solution。

重要：不要把 IF、CASE、GVL_CSP_FAST.xxx := ... 放进上半区，也不要把 PROGRAM MAIN / VAR 放进下半区。之前大量 Unknown type: NOT(...) 之类错误，常见原因就是实现代码粘进声明区。

本例只需要 Tc2_Standard 中的 TON，不需要 Tc2_MC2、MC_Power、NC 轴或额外 PRG_CSP_REPEAT。若报 TON 未定义，右键 References → Add Library，添加 Tc2_Standard。[TON 官方说明](https://infosys.beckhoff.com/content/1033/tcplclib_tc2_standard/74406539.html)

## 4. 任务设置：固定 2 ms，MAIN 只调用一次

1. 双击 PLC 项目中的 PlcTask，检查程序调用列表：只保留一个 MAIN 调用。不需要再手动在 MAIN 中调用 MAIN()。
2. 展开 SYSTEM → Tasks，选择与这个 PLC 对应的 PlcTask。
3. 在 Task 页把右侧实际时间设为 2.000 ms。
4. 若基础时间是 1 ms，则 Cycle ticks = 2；若基础时间不同，按右侧时间调到 2.000 ms，不要机械照抄 ticks。
5. Auto start 可以保持勾选。先保持默认任务优先级。
6. 代码常量 cTaskSec = 0.002，必须与任务实际周期匹配；不要把任务周期改成 1 秒，也不要用任务周期调电机转速。
7. 后续若任务 Exceed counter 增长，记录下来，不能把保护阈值提高当作修复。

[Beckhoff Task 页：Cycle ticks 与基础时间共同决定实际周期](https://infosys.beckhoff.com/content/1033/tc3_system/5210416139.html)

## 5. 扫描 EtherCAT、设置 DC、绑定变量

### 5.1 扫描与同步

1. 电机保持禁止运动，切到 TwinCAT Config 模式。
2. 右键 I/O → Devices → Scan，选择正确 EtherCAT 网卡并扫描从站。
3. 若询问是否自动添加/连接 NC 轴，选 No。本例直接用 PLC PDO，不用 Motion。
4. 从站可能叫 Drive 1，也可能叫 Box 1 (FCM4E1553_CiA402_Drive)，以你实际扫描的名称为准。
5. 选中该从站 → DC 页 → Operation Mode 选 DC-Synchron。
6. 点击该页 Advanced Settings：Sync Unit Cycle = 2000 µs，SYNC0 使用 Sync Unit Cycle ×1（显示 2000 µs）；Enable SYNC0 勾选，Enable SYNC1 不勾选。
7. 先保持你已验证过的 Shift Time；若新工程使用默认 0，就以 0 作为本次初始测试设置。遇到 0x001A 要查同步时序，不要为了跑快随意改 Shift、最小周期或屏蔽同步错误。
8. Build 后确认 EtherCAT 的循环数据由这个 2 ms PlcTask 更新。若 DC 页的 Sync Unit Cycle 不是 2000 µs，先检查任务绑定，不要仅用 User Defined 填 2000 来掩盖任务周期不匹配。

### 5.2 绑定：总共 10 个变量

从设备侧绑定更直观：展开从站的 Outputs / Inputs，右键具体 PDO 变量 → Change Link（或双击 → Linked to / Change Link），在 PLC Instance 中选择下表对应的 GVL_CSP_FAST 变量。

| 从站节点/对象 | PLC 变量 | 类型 |
| --- | --- | --- |
| Outputs → Control Word，0x6040 | qControlWord | UINT |
| Outputs → TargetPosition，0x607A | qTargetPosition | DINT |
| Outputs → TargetVelocity，0x60FF | qTargetVelocity | DINT |
| Outputs → ModeOfOperation，0x6060 | qMode | SINT |
| Inputs → Status Word，0x6041 | iStatusWord | UINT |
| Inputs → ActualPosition，0x6064 | iActualPosition | DINT |
| Inputs → ActualVelocity，0x606C | iActualVelocity | DINT |
| Inputs → ModeOfOperationDisplay，0x6061 | iModeDisplay | SINT |
| 本从站 WcState → WcState | iWcState | BOOL |
| 本从站 InfoData → State | iEthercatState | UINT |

有些旧 XML 会多一层 Module 1，展开后绑定相同叶子节点即可。不要绑定 EtherCAT Device 下面的 Frm0Ctrl、DevCtrl；不要把 InputToggle 当 WcState；不要把主站 DevState 当从站 State。

如果选择列表只显示 Frm0Ctrl/DevCtrl，先确认你编译了新 PLC、展开的是这个从站、且没创建/链接 NC。Only Unused 会隐藏已被绑定的变量；可以取消勾选来查看现状，但不能让 NC 和 PLC 同时写同一 PDO。出现 Mapping conflict 时先去掉新工程中错误的 NC/重复输出绑定，再编译，不要用强制值绕过。

WcState = FALSE/0 表示上一周期通信数据有效；TRUE/1 表示无效。InfoData.State = 8 表示 OP，但该状态的刷新不等同于每周期 WcState，因此故障后实时状态恢复正常不能排除曾经失效。[WcState 官方说明](https://infosys.beckhoff.com/content/1033/b110_ethercat_optioninterface/1984417163.html)

## 6. 激活、登录：先观察，再使能，再运行

1. 再次确认无危险负载、独立急停可用，且旧程序已经停止。
2. TwinCAT → Activate Configuration，按提示重启到 Run 模式。注意激活新配置会替换当前目标机的运行配置。
3. 在这个 PLC 项目选择 Login；若提示下载，下载当前新程序；再选择 PLC → Start。
4. 双击 GVL_CSP_FAST，看到实时值，先不要写 bEnable/bRun。
5. 必须先确认：iWcState = FALSE，iEthercatState = 8，bBusOk = TRUE，bFault = FALSE，qMode = 8。
6. 确认 nCountsPerRev 等于实际每圈计数。首次建议 fMoveTimeSec = 2.0 慢速核对轴标记，确认一圈/无丢步后再用 1.5，最后 1.2。默认代码已设 1.2，但不能跳过机械单位核对。
7. 在“Prepared value / 准备值”一栏把 bResetDiag 准备成 TRUE，点击 PLC → Write values。程序符合安全条件时会清除记录，并自动把请求改回 FALSE。
8. 把 bEnable 准备成 TRUE，执行 Write values。
9. 等待 bReady = TRUE、iModeDisplay = 8；qControlWord 会按 CiA402 状态走 6 → 7 → 15，最后保持 15。CSP 不使用 PP 的 47/63 新设定点触发。
10. 然后单独把 bRun 写成 TRUE，开始周期运动。不要一开始同时写 Enable 和 Run；提前 Run 会被保护拒绝。
11. 观察 qTargetPosition 周期平滑递增、iActualPosition 估算跟随；每圈稳定后 nCompletedTurns 加 1，然后 bPausing 保持约 1 秒，再开始下一圈。

只对命令和参数使用 Write values，不使用 Force values；不要手动改 qControlWord、qTargetPosition、qMode，它们由 MAIN 每周期生成。Force 会在每周期重新覆盖值，可能阻止保护程序撤使能。[Beckhoff 写入与强制的区别](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2527602315.html)

## 7. 如何调快、正常停机、重新开始

调快只改 fMoveTimeSec，不改任务/DC 周期，不改 nCountsPerRev：

| fMoveTimeSec | 单圈指令时间 | 峰值指令转速 | 峰值指令加速度 |
| --- | --- | --- | --- |
| 2.0 | 2 秒 | 45 rpm | 90 rpm/s |
| 1.5 | 1.5 秒 | 60 rpm | 160 rpm/s |
| 1.2（默认） | 1.2 秒 | 75 rpm | 250 rpm/s |

本例允许 1.2～60.0 秒。1 秒需要约 360 rpm/s 的峰值加速度，超过当前默认 300 rpm/s，因此此版不开放小于 1.2 秒；0.5 秒更不能仅改一个参数就保证实现。需要更快时，应先确认电机、负载、母线电压、实际跟随和驱动限值，再单独调整/验证固件与轨迹。

正常停止：先 Write bRun := FALSE。当前这一圈会完成、等待估算停稳，再停在终点，bEnable 仍 TRUE，控制字仍 15。确认 bMoving、bWaitingToSettle 都为 FALSE，再 Write bEnable := FALSE 撤使能。不要把目标位置写回 0 停机。

直接 Write bEnable := FALSE 会立即请求禁止驱动并取消 Run，可能没有受控减速，不能作为安全急停或承重轴保护。通信/驱动故障也会按保护撤使能。

调参数建议在停止并撤使能后进行。程序每圈启动时锁存计数和时间，不在半圈中突然切换轨迹。每次新测试前先在禁止运动、通信正常、估算速度 0 时 Write bResetDiag := TRUE；再重新 Enable，等待 Ready，再 Run。圈数不会被诊断复位清零。

## 8. 又停了：直接看锁存值，不需要 MCU 断点

新例加入 bStopLatched（故障停机锁存）和 bDiagValid（首次事件记录有效）。即使下一周期回到 OP、WcState 恢复 FALSE，diag... 字段也保留故障时的快照。故障后不会自动恢复运动；必须排除问题、手动复位诊断，再重新使能。

nStopReason 是 PLC 条件位掩码，不是 EtherCAT AL 错误码，也不是“已确定的物理根因”：

| 数值/位 | 记录的条件 |
| --- | --- |
| 1 | WcState 无效，哪怕只出现一个周期 |
| 2 | 从站状态不是 OP |
| 4 | 0x6041 的 Fault 位出现 |
| 8 | 使能期间丢失 Operation Enabled/CSP 显示，或未 Ready 就请求 Run |
| 16 | 等待估算到位超过 5 秒 |
| 32 | 相对一圈的起点/终点超出本例 ±20 亿计数窗口 |
| 64 | 一圈启动时，计数/时间参数不符合范围 |
| 128 | bEnable 在外部被撤销 |
| 256 | bRun 在外部被撤销：正常完成当前圈后停止 |

同时发生的条件按位相加，例如 3 = 1 + 2。128/256 只能说明命令请求被外部改变，不能区分人工写入、其他程序或强制值。正常停机也会记录，请在下一轮受控试验前清除记录。1～64 的故障位会锁存禁止运动；正常停止位不会中断当前圈。

停机后先不要复位/下载/重启，截这几项：

- bDiagValid、nStopReason、bStopLatched、nStopCount。
- diagWcState、diagEthercatState、diagStatusWord、diagModeDisplay。
- diagEnableRequest、diagRunRequest、diagControlWord、diagPhase。
- diagTargetPosition、diagActualPosition、diagActualVelocity、diagCompletedTurns、diagCycle。
- TwinCAT 的 Output/Error List，如存在 0x001A，同步问题仍需结合 gEcatSyncDiag 排查。诊断里没有某类记录不等于其他故障不存在。

diagControlWord/diagTargetPosition 是触发前一个 PLC 周期生成的输出命令，输入/请求和 diagPhase 为检测周期进入时的值。记录不是网络抓包，不能凭这一组字段证明 ESC 已收到最后一个输出。

诊断变量非 RETAIN：PLC 下载、Reset、运行时重启/断电可能丢失记录。bResetDiag 只在 Enable/Run 都为 FALSE、通信正常、驱动无 Fault、估算速度为 0 时生效；它不会清除从站 AL 错误或替代 TwinCAT 状态恢复。bResetFault 只有在禁止运动时显式发送一次 0x0080；先找出硬件故障原因，不要反复盲目复位。

## 9. 已完成的离线验证与限制

执行本目录 verify_example.js，可直接解析 MAIN 的本例 ST 子集，逐周期模拟变量、IF/CASE、三次轨迹与 TON，检查 28 项用例：使能握手、单周期 WC 故障锁存、不自动重启、估算到位超时、位置边界、参数检查、正常停止、完整 1 秒暂停、参数逐圈锁存、计数饱和等。

这不是 TwinCAT 编译器，也不是硬件运行。仍必须在你的 TwinCAT 中 Build 成功、确认映射与 2 ms 同步、空载实测，再考虑实际负载。离线通过不证明机械一圈、闭环精度或长期 DC 稳定性。
