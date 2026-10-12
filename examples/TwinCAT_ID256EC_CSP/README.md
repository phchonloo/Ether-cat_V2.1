# 斯达普 ID256-EC：TwinCAT 3 CSP 操作方法

本指南按“新建工程、手动复制粘贴、PLC 直接发送 PDO”的路线编写。不是导入旧工程，不修改你原来的 FCM4E1553 PLC/MCU 程序，也不修改斯达普原始说明书或 XML。

默认只正向转一圈，单圈轨迹约 4 秒。确认方向、脉冲单位和跟随正常后，才打开循环；循环时每圈到位稳定确认后停留 0.5 秒。程序默认禁止运动、不会下载后自动启动。

## 一、资料核对结果：不要直接套用自研驱动器参数

依据用户目录中的：
- [驱动器说明书](C:/Users/pc/Desktop/斯达普/iD256-EC说明书V1.3.pdf)，重点第 16-22 页。
- [厂家 ESI/XML](C:/Users/pc/Desktop/斯达普/ID256EC.xml)。

资料版本注意：PDF 文件名为 V1.3，但实际页眉标为 V1.2；以下以这份文件的实际内容和给定 XML 为依据，不宣称它是厂家最新版本。不同固件如有差异，应向厂家确认。

| 项目 | 给定资料中的内容 |
| --- | --- |
| 驱动器 | ID256-EC，Vendor：SDP (Shanghai) Industrial Corporation Ltd. |
| ESI 身份 | Vendor ID 0x00000DA0，Product Code 0x00000020，Revision 0x00000001 |
| CSP 模式 | 0x6060 = 8，0x6061 应显示 8 |
| 分辨率 | 0x20F2，单位 PPR，默认 10000，范围 200-102400 |
| 目标/实际位置 | 0x607A / 0x6064，DINT，单位 pulse |
| 支持同步周期 | 500、750、1000、2000、4000 µs；本例用 2000 µs |
| CSP 控制字流程 | 0 → 6 → 7 → 15 → 31（十六进制 00 → 06 → 07 → 0F → 1F） |
| 状态字跟随位 | 0x6041 bit12 = 1，表示 CSP 跟随主站 |
| 默认输出 PDO | 0x1600 / RxPDO 1，13 字节 |
| 默认输入 PDO | 0x1A00 / TxPDO 1，17 字节 |

重要区别：
1. 每圈脉冲数不是自研示例的 1000。必须在线读取实际 0x20F2；默认值 10000 不能替代实机读取。位置缩放还要用轴标记/实际角度验证。
2. 按该手册第 22 页的运行表，本例运行控制字使用 31。第 21 页同时把 CSP 的 bit4 写为无效，所以不要把 31 解释成 PP 的“新设定点触发”；本例只是遵循厂家给出的运行值，不把它推广到其他驱动器。
3. CSP 的位置轨迹由主站规划，本例由 PLC 实现；厂家表中的“激活 NC 轴”是另一种主站实现方式，并非本例必须添加 NC。
4. 本例不依赖 0x6041 bit10 判定 CSP 到位：该手册 CSP 的 bit10 标为无效，而 bit12 是跟随状态，不是已经到达机械终点。
5. 资料第 20 页把 0x606C 写为 P/s；第 18 页另写 PRS，存在单位记法不一致。本例按 P/s 口径使用低速阈值，不使用自研驱动器的 rpm 反馈假设。不同固件若有缩放差异需确认。
6. 第 17 页说明 2xxx 参数用 SDO 写入会自动保存 EEPROM，写入寿命有限。0x20F2 等配置参数仅按需要设置一次，不得放进 PLC 每周期反复写。

## 二、什么是 CSP，怎么调速

CSP = Cyclic Synchronous Position，循环同步位置模式。每个总线周期给驱动器一个新的目标位置，驱动器按同步节拍跟随。

例如当前 0x20F2 为 10000：
- 一圈目标位置增量：10000 pulse。
- 四分之一圈：2500 pulse。
- 反转一圈：位置减 10000 pulse。
- 每圈不是把目标写成固定 10000；应从当前目标/起点向前加 10000。
- 不能把目标位置瞬间从 0 跳到 10000，再等它转完，就认为完成了平滑 CSP 轨迹。

本例每 2 ms 输出三次位置曲线，单圈时间用 fMoveTimeSec 决定。首次设 4.0 秒：平均约 15 rpm，理想曲线峰值约 22.5 rpm。该速度仅为建议的初始空载验证参数，不是厂家承诺的负载安全值。

不要靠缩短 PLC/DC 周期来加速。0x6081/0x6083/0x6084 在手册中主要用于 PP/PV 轨迹，本例 CSP 的速度和加减速首先由 PLC 位置曲线决定，不能照搬自研驱动器的限速逻辑。

## 三、准备与新建工程

### 3.1 硬件准备

1. 停止旧工程的运动；先做独立、空载、无危险机构的台架试验。
2. 使用配套电机、正确编码器接线和符合铭牌的电源；核对电流、电机配置和限位接线。不要带电插拔电机/电源端子。
3. 外部急停与断电手段必须可用；软件 bEnable/bRun 不是安全急停，也不提供 STO。
4. 电脑 EtherCAT 专用网卡接驱动器 EtherCAT IN；不要接错为普通调试口。关闭其他试运行命令源。
5. 本例先仅测试 ID256-EC 一个轴，避免把自研 Box 1 的变量绑定到这款驱动器。

### 3.2 安装 ESI

将给定 ID256EC.xml 放入当前 TwinCAT 安装实际使用的 EtherCAT ESI 目录。旧版常见：
C:\TwinCAT\3.1\Config\Io\EtherCAT

新安装布局也可能使用：
C:\Program Files (x86)\Beckhoff\TwinCAT\3.1\Config\Io\EtherCAT
或用户目录下的 Beckhoff/TwinCAT 配置目录。

不要因为目录不同就重新创建一个无人使用的目录；应确认当前 XAE 的安装路径。在 TwinCAT → EtherCAT Devices → Reload Device Descriptions 重新加载，或重启 XAE。先备份同名旧 ESI，再按实际设备版本处理，不必删除其他厂家 XML。

官方参考：[旧安装布局的 ESI 目录](https://infosys.beckhoff.com/content/1033/el70x7/1036998411.html)，[新布局第三方 ESI 的位置](https://infosys.beckhoff.com/content/1033/tc3_automationinterface/21840443915.html)。

### 3.3 新建 XAE 和 PLC

1. File → New → Project → TwinCAT XAE Project。
2. 名称填 ID256_CSP_Test，使用新目录。
3. 右键 PLC → Add New Item → Standard PLC Project。
4. PLC 名称填 ID256_CSP_PLC。
5. 保留自动生成的 MAIN 和 PlcTask；不用导入 TcPOU/TcGVL。
6. 不创建/连接 NC 轴；扫描提示自动添加 NC 轴时选 No。

[Beckhoff 新建 Standard PLC Project](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2526130571.html)

## 四、复制代码：三个文件，三个粘贴位置

### 4.1 GVL

右键 ID256_CSP_PLC Project → GVLs → Add → Global Variable List。
名称必须为 GVL_ID256_CSP。
打开 [GVL_ID256_CSP.st](C:/Users/pc/Desktop/01_FCM4E1553_SQI_82_CTT_V1.4/examples/TwinCAT_ID256EC_CSP/GVL_ID256_CSP.st)，复制全部，替换新 GVL 的空模板。

### 4.2 MAIN 声明区

双击 POUs → MAIN (PRG)，语言为 ST。
打开 [MAIN_Declaration.st](C:/Users/pc/Desktop/01_FCM4E1553_SQI_82_CTT_V1.4/examples/TwinCAT_ID256EC_CSP/MAIN_Declaration.st)，复制全部，粘贴进 MAIN 上半区，替换原来的 PROGRAM MAIN / VAR / END_VAR 声明。

### 4.3 MAIN 实现区

打开 [MAIN_Implementation.st](C:/Users/pc/Desktop/01_FCM4E1553_SQI_82_CTT_V1.4/examples/TwinCAT_ID256EC_CSP/MAIN_Implementation.st)，复制全部，粘贴进 MAIN 下半区，替换空实现。

上半区只放声明；下半区放赋值、IF、CASE、TON 调用。不要反过来，否则容易出现 Unknown type: NOT(...) 等大量错误。

保存，Build → Build Solution。如果 TON 未定义，References → Add Library 添加 Tc2_Standard；本例不用 Tc2_MC2。

## 五、任务、PDO、DC 设置

### 5.1 PLC 任务

SYSTEM → Tasks → 对应的 PlcTask → Task：
- 实际 Cycle Time：2.000 ms。
- 基础时间为 1 ms 时，Cycle ticks = 2；以右侧实际毫秒为准。
- MAIN 只调用一次。
- 代码 cTaskSec = 0.002，必须与任务实际周期相同。
- 不额外添加另一个 PRG 来再次调用同一运动逻辑。

### 5.2 扫描与默认 PDO

进入 TwinCAT Config，扫描 EtherCAT 网卡与驱动器。确认设备名 ID256-EC，以及实际身份与 ESI 匹配。设备树可能叫 Drive 1/Drive 2，以真实 ID256-EC 节点为准，不是 FCM4E1553。

Process Data 初次保留默认：
- SM2 / Outputs：0x1600，RxPDO 1。
- SM3 / Inputs：0x1A00，TxPDO 1。

只用 CSP 不需要额外启用 0x1601/0x1602/0x1A01。原 XML 已经把控制字、目标位置、模式及主要反馈放进默认 PDO。

从设备侧右键具体 PDO 叶子变量 → Change Link，选择新 PLC Instance 中的 GVL_ID256_CSP：

| 设备树变量 | 对象 | PLC 变量 | 类型 |
| --- | --- | --- | --- |
| RxPDO 1 → Control Word | 6040:00 | qControlWord | UINT |
| RxPDO 1 → Target Position | 607A:00 | qTargetPosition | DINT |
| RxPDO 1 → Target Velocity | 60FF:00 | qTargetVelocity | DINT |
| RxPDO 1 → Touch Probe Function | 60B8:00 | qTouchProbeFunction | UINT |
| RxPDO 1 → Modes of Operation | 6060:00 | qMode | SINT |
| TxPDO 1 → Error register | 603F:00 | iErrorCode | UINT |
| TxPDO 1 → Status Word | 6041:00 | iStatusWord | UINT |
| TxPDO 1 → Modes of Operation Display | 6061:00 | iModeDisplay | SINT |
| TxPDO 1 → Position Actual Value | 6064:00 | iActualPosition | DINT |
| TxPDO 1 → Velocity Actual Value | 606C:00 | iActualVelocity | DINT |
| TxPDO 1 → Digital Inputs | 60FD:00 | iDigitalInputs | UDINT |
| 本驱动器 WcState → WcState | 主站诊断 | iWcState | BOOL |
| 本驱动器 InfoData → State | 主站诊断 | iEthercatState | UINT |

共 5 个输出、8 个输入，含两项主站诊断。不使用探针时，程序仍把 Touch Probe Function 每周期写 0；不要绑定到其他程序。

注意：XML 把 603F 命名为 Error register，但它不是 1001 的 8 位通用错误寄存器，iErrorCode 必须为 UINT。此处按对象索引区分，不只看名字。

不能同时让 NC、另一个 PLC 程序和此 MAIN 写同一输出 PDO。Mapping conflict 要解决重复绑定，而不是强制输出值。WcState 绑定本驱动器的 WcState，不是 InputToggle；State 绑定本驱动器 InfoData.State，不是主站 DevState。

### 5.3 DC

1. 选 ID256-EC → DC 页，选择 ESI 提供的 DC-Synchron。
2. Advanced Settings 中确认 Sync Unit Cycle = 2000 µs，SYNC0 为该周期 ×1。
3. XML 的该预设 AssignActivate 为 0x0300。先按厂家 ESI 预设配置；不要手工改成自研固件的同步配置。
4. 不自行额外启用 SYNC1、不复制其他驱动器 Shift 参数；初次按 ESI 的默认 Shift，记录实际值。
5. 如果任务为 2 ms，而 Sync Unit Cycle 显示 1 ms，先检查实际 EtherCAT 数据所属任务；不要只手填一个 SYNC0 周期掩盖任务不匹配。
6. 后续比较 SM-Synchron 时，应在停机、禁止运动后切换并重新激活配置，不能转动中切换作可靠性判断。切模式不是修复通信/保护故障的方法。

### 5.4 模式与每圈计数

该 XML 的 PS（PREOP→SAFEOP）启动命令已写 6060:00 = 08，进入 CSP。本例同时把 qMode 持续写为 8。

通过 CoE Online 的 Online 数据读取：
- 20F2:00 分辨率（只读实际值，不周期写入）。
- 6061:00 模式显示。
- 如需要，再核对 20F7 到位范围、607D 软件限位及 60FD 输入状态。

如果 20F2 的实际值是 10000，那么 GVL 中 nCountsPerRev = 10000；如果实际为 20000，则填 20000。XML DefaultData 的 10270000 是小端字节串，对应数值 0x2710 = 10000，不能把字节串当十六进制数值直接写进参数。

必须通过空载轴标记确认位置增量确实对应机械一圈；齿轮/丝杆情况下还需换算传动比，不把电机一圈误认为负载一圈。

## 六、激活、登录、首次单圈

1. 停止旧工程，确认危险机构已隔离、独立急停可用。
2. TwinCAT → Activate Configuration，按提示进入 Run。这会替换目标机原运行配置，操作前保留旧工程。
3. PLC Login，下载这个新 PLC；PLC → Start。
4. 打开 GVL_ID256_CSP 在线表。

先检查：
- iWcState = FALSE，iEthercatState = 8，bBusOk = TRUE。
- bFault = FALSE；结合 iErrorCode、实际报警灯确认没有当前故障。
- nCountsPerRev 与实际 20F2 相符。
- fMoveTimeSec = 4.0。
- bRepeat = FALSE，bEnable = FALSE，bRun = FALSE。

操作都用“Prepared value / 准备值 → PLC → Write values”，不用 Force values。

依次：
1. 禁止运动、通信正常、反馈速度 0 时，把 bResetDiag 写 TRUE；符合条件会自动回 FALSE。
2. 把 bEnable 写 TRUE。
3. 等待 bReady = TRUE。
4. 核对 iModeDisplay = 8，qControlWord = 31；状态字 bit12 应为 1。
5. 把 bRun 写 TRUE，程序规划一圈。
6. 到终点并满足稳定反馈条件后，nCompletedTurns 增加 1，bRun 自动回 FALSE，bEnable 仍 TRUE，控制字仍 31，保持当前位置。
7. 观察轴标记、反馈变化和运动方向，确认一圈成立，再继续。

如果 bReady 一直 FALSE，不要强制为 TRUE。先查绑定、OP、6061=8、6041 的 CiA402 状态和 bit12；把状态字原值截图，而不是死等它必须等于某一个完整数值。手册典型值为 0x1237，但其他诊断位可能改变，程序按位判断。

[Beckhoff Write values 与 Force values 的区别](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2527602315.html)

## 七、循环、调速、正常停止

### 循环每圈停 0.5 秒

单圈验证后，把 bRepeat 写 TRUE，再写 bRun = TRUE：
约 4 秒一圈 → 反馈稳定确认 100 ms → 保持目标不变 0.5 秒 → 下一圈。

Pause 常量在 MAIN 上半区：
cPauseTime : TIME := T#500ms;

这 0.5 秒是到位确认后的保持时间，未把实际跟随/停稳时间算入。暂停仍继续每 2 ms 发送 PDO 和相同目标位置，不能停止总线帧来“暂停电机”，否则可能触发看门狗。

### 调速

先正常停止、撤使能，再改 fMoveTimeSec，初次可依次验证 4.0 → 3.0 → 2.0 秒。本例允许 2-60 秒，是示例的初始验证范围，不是厂家能力上限，也不保证你的负载可以用 2 秒。

| 单圈时间 | 平均转速 | 理想曲线峰值转速 | 理想曲线峰值加速度 |
| --- | --- | --- | --- |
| 4 秒 | 15 rpm | 22.5 rpm | 22.5 rpm/s |
| 3 秒 | 20 rpm | 30 rpm | 40 rpm/s |
| 2 秒 | 30 rpm | 45 rpm | 90 rpm/s |

参数每圈开始时锁存，不在半圈突然替换轨迹。要更快应先验证电机、负载、电流、跟随误差和驱动限值，再改变示例验证范围；不要只缩短周期或提高故障阈值。

### 正常停止

- 循环时写 bRun := FALSE：当前圈完成、等待到位稳定后停住，不再开始新圈。
- 单圈模式完成后，bRun 自动 FALSE，不属于故障停机，bDiagValid 不会因此置位。
- 确认 bMoving、bWaitingToSettle 都 FALSE，再写 bEnable := FALSE。
- 不把 qTargetPosition 写回 0 来停止；也不使用 PP 的 47/63 控制字序列来触发 CSP。

直接撤 bEnable 请求会立即禁止运动，可能没有受控减速，不能用作安全急停或承重轴保护。不要绕过硬件限位、欠压、过流等保护。

## 八、验证 CSP 可用，不只看“能转”

逐项记录：
1. 模式显示稳定为 8，CiA402 Operation Enabled，状态字 bit12 跟随主站。
2. 目标位置按 2 ms 周期平滑变化，实际位置跟随；不是一个大位置跳变。
3. 一圈命令增量 = 当前 20F2，实际轴标记/角度匹配。
4. 到位后的位置误差、停止时速度、重复定位误差可接受。
5. 本例 bAtTarget 判据：位置误差 ≤5 pulse，|反馈速度|≤5 pulse/s，连续 100 ms。应结合实际 20F7 和允许误差调整，不能把程序 TRUE 直接当作机械精度合格证明。
6. 反复使能、撤使能和重启不出现位置跳变或未经授权重新运动。
7. 无 WcState 异常、任务 Exceed 增长或从站掉状态；持续运行时保存 PLC 和驱动器诊断。

本例先只正向验证；需要反转时应另做带方向和限位检查的轨迹，不把 nCountsPerRev 改成负数（它表示正的每圈分辨率，本例不接受负数）。

## 九、如果又自行停止：保存记录再分析

该独立示例加入：
- bStopLatched：故障锁存，恢复 OP 不会自动重新运行。
- bDiagValid / nStopReason：首个事件条件及位掩码。
- diagErrorCode / diagDigitalInputs：该周期错误码和输入字。
- diagStatusWord / diagModeDisplay / diagWcState / diagEthercatState。
- diagControlWord / diagTargetPosition / diagActualPosition / diagActualVelocity / diagPhase。
- diagEnableRequest / diagRunRequest / diagCycle。

nStopReason 是 PLC 的触发条件，不是厂家报警码，不代表已确定物理根因：

| 位值 | 条件 |
| --- | --- |
| 1 | 通信数据无效，哪怕仅一周期 |
| 2 | 从站不是 OP |
| 4 | 状态字 Fault 位 |
| 8 | 丢失 Operation Enabled、模式 8 或 bit12 跟随，或未 Ready 就 Run |
| 16 | 轨迹完成后等待到位超过 5 秒 |
| 32 | 起点/终点超出本例 ±20 亿 pulse |
| 64 | 每圈计数或运动时间参数不符合范围 |
| 128 | 外部撤销 Enable |
| 256 | 外部撤销 Run 的正常停止请求 |
| 512 | 状态字 bit8 非正常停止，如硬限位/快停触发 |

同时发生则按位相加，例如 3 = 1+2，520 = 512+8。1-64/512 的故障条件会锁存停机；128/256 只记录请求改变，不额外中断正常收尾轨迹。

停机后先不要复位、下载或重启，截 bDiagValid、nStopReason 和 diag... 字段，以及 TwinCAT Output/Error List。如果 nStopReason 包含 4，重点核对锁存的 diagErrorCode（603F）及厂家报警；包含 512/8 时结合原始状态字和 60FD 限位输入判断，不能先屏蔽保护。

本例没有读取到完整的 603F 报警枚举表，所以不凭通用 CiA402 码直接断言厂家具体故障含义；必要时记录 206C 的厂家错误码、面板/LED 状态和当前硬件/软件版本，交给厂家确认。

diagControlWord/diagTargetPosition 是进入故障检测周期前一次程序生成的命令，不证明网络上已经送达。所有记录非 RETAIN，PLC 下载/Reset/重启可能清掉。bResetDiag 只在 Enable/Run FALSE、通信正常、无 Fault、速度为 0 时生效；bResetFault 仅显式发送一次 0x0080，不能当成自动循环清错手段。bResetDiag 不清从站故障，也不替代恢复 EtherCAT 状态。

## 十、如果想用 Motion/NC

可以用 TwinCAT NC 做轨迹，再用 MC_Power、MC_MoveRelative 等功能块操作。这是另一条路线：
- NC 轴绑定 ID256-EC，模式仍为 CSP。
- 按实际位置单位和每圈脉冲数设置比例；若用角度单位，每个 pulse 对应 360 / 实际每圈脉冲数 度；有传动机构还要考虑传动比。
- 不能直接把 XML 的 EncoderResolution（默认 4000）当成 607A/6064 的每圈命令脉冲数；它和 20F2（默认 10000）的逻辑分辨率是不同参数。
- PLC 只给 NC 运动命令，不再把本例 qControlWord/qTargetPosition 绑定同一从站。
- 两条路线只能选一个输出命令源。本指南的全部粘贴步骤针对“PLC 直接 PDO”，不混用 NC。

## 十一、离线验证范围

verify_example.js 已通过 24 项源码驱动的离线逻辑检查，覆盖与给定 ESI 的默认 PDO 一致性、控制字 6/7/15/31、跟随位、单圈自动停止、0.5 秒暂停循环、非零位置首次对齐、通信/驱动/异常停止诊断、超时和参数/位置边界。

它解释本例 ST 子集和 TON 模型，不是 TwinCAT 编译器，不是厂家认证，不是实机运行。仍需在你的 TwinCAT 中编译成功、核对映射和周期、空载验证，再测试实际负载。
