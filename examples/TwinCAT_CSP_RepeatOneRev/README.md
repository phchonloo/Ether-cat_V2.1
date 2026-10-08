# CSP 示例：转一圈，停稳后等 1 秒，再转一圈

本例用 PLC 直接控制当前驱动器的 CSP PDO，循环正转：
转一圈 → 根据开环反馈确认停稳 → 保持当前位置 1 秒 → 再转一圈。

默认每圈计数为 1000，每圈轨迹用时 2 秒，暂停固定 1 秒。
采用三次平滑位置曲线，单圈平均速度 30 rpm、峰值约 45 rpm，轨迹峰值加速度约 90 rpm/s。
停稳确认还要求反馈连续满足到位条件 100 ms，所以两圈启动间隔并非严格 3 秒。
这里不再是上一份每秒一圈、始终不停的示例。

本例仅新增示例，不修改 MCU 固件或 ESI。原生文件的结构和模型可本地自检，
但尚未在 TwinCAT 中实际导入、编译或实机验证。它不替代急停、安全回路或完整机器保护。

## 1. 推荐：导入原生文件，避免分区粘贴报错

本目录提供两种等价格式，只选一种，不要重复添加同名对象：

| 用途 | 推荐导入文件 | 手动复制备选 |
|---|---|---|
| 全局变量表 | GVL_CSP_REPEAT.TcGVL | GVL_CSP_REPEAT.st |
| MAIN 程序 | MAIN.TcPOU | MAIN_Declaration.st 与 MAIN_Implementation.st |

推荐在新的测试 PLC 项目中操作，保留已有控制程序。

1. 扫描当前从站，使用当前固件及根目录的 FCM4E1553_CiA402_SingleAxis_Rev03.xml。
2. 新建 Standard PLC Project，例如 CSP_REPEAT_Test。
3. 该例不使用 NC：解除 Motion/NC 和其他示例对本驱动器输出的链接。
4. 先将新项目的空模板 MAIN 重命名为 MAIN_Backup，保留原文件。
5. 在 POUs 文件夹右键 Add → Existing Item...，选择本目录的 MAIN.TcPOU。
6. 在 GVLs 文件夹用 Add → Existing Item...，选择 GVL_CSP_REPEAT.TcGVL。
7. 检查 PlcTask 的程序调用：只调用新的 MAIN 一次，不调用 MAIN_Backup。
8. 在 References 中检查 Tc2_Standard；若没有，用 Add Library 添加。TON 来自这个库。
9. Build PLC。先解决编译错误，再激活或使能电机。

这两个文件不是完整 .tsproj 工程，不包含目标电脑、网卡或 I/O 映射。
若导入遇到对象重名，处理测试项目里的同名对象，不要直接删除已有实际控制程序。

### 手动复制时的关键区别

- GVLs 中创建名称严格为 GVL_CSP_REPEAT 的全局变量表，放入 GVL_CSP_REPEAT.st。
- MAIN 的上方声明编辑区只放 MAIN_Declaration.st，从 PROGRAM MAIN 到最后的 END_VAR。
- MAIN 横向分隔线下方的 ST 执行编辑区只放 MAIN_Implementation.st。
- 在上方编辑区的 END_VAR 后面继续粘贴，并不等于进入实现区。
- 若出现 Unknown type: NOT(...)，优先检查是否把执行代码贴到了声明区。
- MAIN 必须是 ST 程序，不是 GVL、DUT 或其他语言对象。

## 2. 周期与 PDO 映射

PlcTask 和关联 EtherCAT 更新周期都设置为 2 ms。
MAIN 中 cTaskSec=0.002 必须与真实周期一致；不是把任务周期设置成 1 秒。
初次测试保留已经稳定的 SM 同步配置，再单独验证 DC。
Task 超时会破坏轨迹与真实时间的关系，不能忽略。

Build 后，在实际驱动器的 I/O 变量上 Change Link，关联以下变量。
禁止 NC、旧 CSP/CSV 示例和本例同时写同一输出。

| 从站变量 | 索引/位置 | PLC 变量 | 类型 |
|---|---|---|---|
| Outputs / Control Word | 0x6040 | GVL_CSP_REPEAT.qControlWord | UINT |
| Outputs / TargetPosition | 0x607A | GVL_CSP_REPEAT.qTargetPosition | DINT |
| Outputs / TargetVelocity | 0x60FF | GVL_CSP_REPEAT.qTargetVelocity | DINT |
| Outputs / ModeOfOperation | 0x6060 | GVL_CSP_REPEAT.qMode | SINT |
| Inputs / Status Word | 0x6041 | GVL_CSP_REPEAT.iStatusWord | UINT |
| Inputs / ActualPosition | 0x6064 | GVL_CSP_REPEAT.iActualPosition | DINT |
| Inputs / ActualVelocity | 0x606C | GVL_CSP_REPEAT.iActualVelocity | DINT |
| Inputs / ModeOfOperationDisplay | 0x6061 | GVL_CSP_REPEAT.iModeDisplay | SINT |
| WcState | 本从站诊断输入 | GVL_CSP_REPEAT.iWcState | BOOL |
| InfoData / State | 本从站状态输入 | GVL_CSP_REPEAT.iEthercatState | UINT |

不要绑定 EtherCAT 主站的 Frm0Ctrl、DevCtrl。
当前 ESI 的 PDO 可能直接在从站下；旧版可能在 Module 1 下，按实际名称和索引确认。
只在 CoE 写一次模式 8 可能被 PDO 覆盖，本例每周期发送 qMode=8。
稳态 qControlWord=15，CSP 不需要 PP 的 47 → 63 触发。

## 3. 使用步骤

1. 使用轴端有标记、运动范围安全的空载测试台，准备独立停机手段。
2. Activate Configuration，TwinCAT Run；PLC Login、下载、Start。
3. 确认 bBusOk=TRUE、bFault=FALSE，iWcState=FALSE，从站状态为 OP。
4. 检查 nCountsPerRev。源码 sxifen 默认为 1000，但必须匹配实际运行细分。
5. 保持 fMoveTimeSec=2.0；本例只接受 2～60 秒，避免默认内部斜坡跟不上过快轨迹。
6. 用 Prepared value 和 PLC → Write values 写 bEnable=TRUE。
7. 等 bReady=TRUE、iModeDisplay=8。
8. 写 bRun=TRUE，开始重复运行。
9. 第一次目标相对当前保持位置增加 1000，第二次再增加 1000，以此类推，不回到零。
10. 正常停止重复时写 bRun=FALSE。当前一圈会完成、确认停稳后停止，不再发下一圈。
11. 若已经处于暂停阶段，取消 bRun 会取消后续循环。
12. 完全停止后按需要写 bEnable=FALSE。

bEnable 和 bRun 默认均为 FALSE。未 Ready 时写入的 bRun 会被清除，不排队等启动。
不要使用 Force values 强制操作变量或 PDO，这可能覆盖程序取消运行的逻辑。
不要把 qTargetPosition 写回零当作停止命令。

注意：bRun=FALSE 不是立即停止，也不是急停；它是“当前一圈做完后结束循环”。
直接撤销 bEnable 会取消使能，不属于完成当前一圈的正常停止流程。

## 4. 怎样知道它在做什么

| nPhase | 行为 | 对应监视量 |
|---|---|---|
| 0 | 待机，保持目标 | 等待启动或已经停止循环 |
| 1 | 生成本圈位置轨迹 | bMoving=TRUE |
| 2 | 等反馈到位、零速并连续稳定 100 ms | bWaitingToSettle=TRUE |
| 3 | 保持目标，等待 1 秒 | bPausing=TRUE，tPauseElapsed 从 0 到 1 秒 |

nCompletedTurns 是已确认停稳的圈数，不是定时器触发次数，也不是真实编码器计数。
暂停期间反馈不再满足到位/零速，会重新进入停稳确认，随后重新等满 1 秒。
同一圈不会因此重复计数。

fCommandRpm 是 PLC 轨迹速度，默认单圈峰值约 45，不是测速仪读数。
完成一圈后目标位置保持不变；必须反馈到位并且 iActualVelocity=0，才开始暂停。
0x6064 和 0x606C 在当前工程里来自开环算法，不能证明机械轴无丢步或真实到位。
请用轴上标记验证每次实际转一圈，观察圈与圈之间确实停止约一秒以上。

## 5. 参数、保护和恢复

nCountsPerRev：每圈计数，默认 1000，必须大于 0 且不超过 1000000。
fMoveTimeSec：每圈轨迹时间，默认 2.0，范围 2.0～60.0。
参数在每圈开始时锁存，运行中修改不会改变当前轨迹，但可能影响下一圈。
最好停止循环后再修改参数，不能边运行边改变 MCU 的细分参数。

cPauseTime 固定 T#1s，cSettleTime 为 T#100ms，cArrivalTimeout 为 T#5s。
如果要改圈间间隔，停止后修改 cPauseTime 再编译。

- bConfigError：参数无效。当前已启动轨迹会完成，但不开始下一圈；修正参数后重新启动。
- bArrivalTimeout：本圈轨迹发完 5 秒仍未确认停稳，程序禁用并取消循环。
- bPositionLimit：下一圈目标超过示例正负 2000000000 的边界，拒绝运动并禁用。
- 通信异常或驱动故障：取消 bEnable、bRun，不会恢复后自动续跑。
- bArrivalTimeout/bPositionLimit 是 PLC 示例报警，不是 MCU 的 0x603F 错误码。

恢复 PLC 示例报警：保持 bEnable=FALSE，确认通信正常、没有驱动故障且估算速度为零，
写 bResetSequence=TRUE 清除报警，再重新使能，等待 Ready，重新写 bRun=TRUE。
若起点仍在边界附近，位置保护会再次触发；本例不实现无限位置回绕，不能强制绕过。

驱动器故障要先排除原因，再写 bResetFault=TRUE，程序发送 0x0080。
故障清除后重新使能和启动。计数器保留此前已完成圈数，重启 PLC 会回到默认值。

当前固件 CSP 仍受 0x6081 限速和 0x6083/0x6084 内部斜坡限制。
默认 100 rpm 与 300 rpm/s 可容纳本例 2 秒轨迹的理论峰值，但实际机械表现仍需测试。
不要仅修改估算反馈值来掩盖丢步或跟不上。

## 6. 自检与资料

在本目录执行 node verify_example.js。
它检查源文件/原生文件一致性、标识符引用，以及循环、停留和异常分支的数值模型。
模型不是 ST 编译器，不验证真实总线时序、MCU 控制或 TwinCAT 导入兼容性。

- [TwinCAT 添加已有 PLC 对象](https://infosys.beckhoff.com/content/1033/tc3_userinterface/7737742859.html)
- [TON 定时器](https://infosys.beckhoff.com/content/1033/tcplclib_tc2_standard/74406539.html)
- [任务周期配置](https://infosys.beckhoff.com/content/1033/tc3_system/5210416139.html)
- [声明编辑器](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2528211083.html)
