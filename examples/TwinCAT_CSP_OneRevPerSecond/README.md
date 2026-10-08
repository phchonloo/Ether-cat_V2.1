# TwinCAT CSP 示例：稳定后每秒一圈

本例由 PLC 直接写 EtherCAT PDO，在 `0x6060=8` CSP 模式下连续正转。
默认每圈 1000 个位置计数，PLC 和关联 EtherCAT 更新周期均为 2 ms。
稳态目标位置每周期增加 2，500 个周期累计增加 1000，即目标轨迹为 60 rpm。

不是每隔一秒突然把 0x607A 加 1000，也不是转一圈后停下来。
启动和正常停止使用 120 rpm/s 的轨迹斜坡，约 0.5 秒达到 60 rpm，第一秒不严格一圈。
“每秒一圈”指稳态目标轨迹，不能保证开环机械轴精确按秒转满一圈。

本例不修改 MCU 固件和 ESI。三个 `.st` 文件供复制到 TwinCAT 编辑器，不是完整 `.tsproj` 工程。
尚未在 TwinCAT 编译器或实际驱动器上验证。本例不替代急停、安全回路或完整机器保护。

## 1. 准备

1. 使用空载、轴端有标记且运动范围安全的测试台，准备独立停机手段。
2. 保持电机未使能，不要边运行边修改细分、周期或每圈计数。
3. 使用当前固件及根目录的 `FCM4E1553_CiA402_SingleAxis_Rev03.xml`。
4. 不使用 NC 轴；若已有 Motion/NC 或旧 CSP/CSV 示例，解除它们对本从站输出的绑定。
5. 本例单独运行，不能与其他程序同时写同一个驱动器的 PDO。
6. 初次测试保留已经稳定的 SM 同步配置；先跑通，再单独验证 DC。

## 2. 创建 PLC 程序

1. 新建 TwinCAT 测试工程，扫描当前驱动器。
2. 右键 `PLC → Add New Item... → Standard PLC Project`，命名例如 `CSP_1RPS_Test`。
3. 在 PLC 项目的 `GVLs` 中新增 Global Variable List，名称必须是 `GVL_CSP_1RPS`。
4. 把 `GVL_CSP_1RPS.st` 全部复制到该 GVL。
5. 打开 `POUs → MAIN`，声明区替换为 `MAIN_Declaration.st`，实现区替换为 `MAIN_Implementation.st`。
6. MAIN 使用普通 ST；不需要添加 Tc2_MC2，本例不通过 NC 控制。

## 3. 核对周期和每圈计数

在 `SYSTEM → Tasks → PlcTask` 任务页调整 Cycle ticks，使实际周期显示 **2.000 ms**。
基准时间 1 ms 时是 2 ticks，100 μs 时是 20 ticks；不要把任务设置成一秒。
确认 MAIN 每周期由该任务调用一次，没有第二个任务也执行 MAIN。

| 声明区常量 | 默认值 | 意义 |
|---|---|---|
| cTaskSec | 0.002 | PLC 任务周期，秒 |
| cCountsPerRev | 1000.0 | 实际每圈计数，必须匹配运行时 sxifen |
| cRunRpm | 60.0 | 一秒一圈，保持此值 |
| cAccelRpmPerSec | 120.0 | 轨迹加速度，rpm/s |
| cDecelRpmPerSec | 120.0 | 正常停止轨迹减速度，rpm/s |
| cOneSecondCycles | 500 | 一秒包含的任务周期数，诊断使用 |
| cPositionStopLimit | 2000000000.0 | 接近 DINT 上界前停止 |

源码 SaveFunc.c 默认 sxifen=1000，但实际运行参数可能已经改变。
若每圈 1600 个计数，cCountsPerRev 应为 1600.0，每周期稳态增量为 3.2。
程序用 LREAL 累计完整位置，再转 DINT，避免每周期舍掉小数造成速度偏差。

改变任务周期必须同步修改 cTaskSec 和 cOneSecondCycles；例如 1 ms 对应 0.001 和 1000。
常量必须保持正值，周期应能整除一秒；初次测试不要修改默认周期。
任务超时会破坏真实时间与位置增量的关系，本例不会自动测量实际任务间隔。

## 4. 编译并关联 PDO

Build PLC 项目，让 PLC 实例下生成 Inputs/Outputs。
在实际从站 I/O 变量上使用 `Change Link...`，逐一关联：

| 从站变量 | 索引/位置 | PLC 变量 | 类型 |
|---|---|---|---|
| Outputs / Control Word | 0x6040 | GVL_CSP_1RPS.qControlWord | UINT |
| Outputs / TargetPosition | 0x607A | GVL_CSP_1RPS.qTargetPosition | DINT |
| Outputs / TargetVelocity | 0x60FF | GVL_CSP_1RPS.qTargetVelocity | DINT |
| Outputs / ModeOfOperation | 0x6060 | GVL_CSP_1RPS.qMode | SINT |
| Inputs / Status Word | 0x6041 | GVL_CSP_1RPS.iStatusWord | UINT |
| Inputs / ActualPosition | 0x6064 | GVL_CSP_1RPS.iActualPosition | DINT |
| Inputs / ActualVelocity | 0x606C | GVL_CSP_1RPS.iActualVelocity | DINT |
| Inputs / ModeOfOperationDisplay | 0x6061 | GVL_CSP_1RPS.iModeDisplay | SINT |
| WcState | 本从站诊断输入 | GVL_CSP_1RPS.iWcState | BOOL |
| InfoData / State | 本从站状态输入 | GVL_CSP_1RPS.iEthercatState | UINT |

不要关联 Device 3 的 Frm0Ctrl、DevCtrl 等主站控制项。
旧 ESI 的变量可能在 Module 1 下，新 ESI 可能直接在从站下，按实际名称及索引确认。
Mapping conflict 要解除旧输出绑定；取消 Only Unused 只能显示旧绑定，不能消除冲突。
其他没有 AT 声明的变量都是 PLC 内部控制或监视变量，不绑定 PDO。

打开 EtherCAT 主站 `EtherCAT → Advanced Settings... → Cyclic Frames → Sync Tasks`，
检查本从站过程数据由 PlcTask 按 2000 μs 更新；通过 `Sync Unit Assignment...` 核对关联。
只在 CoE 写一次 0x6060=8 可能被 PDO 覆盖，本例每周期由 qMode 持续发送 8。

## 5. 激活、使能、启动

1. Activate Configuration，确认目标 TwinCAT 重启到 Run。
2. PLC Login，按提示下载，再执行 PLC Start。
3. 在线打开 GVL_CSP_1RPS；bEnable 和 bRun 默认均为 FALSE。
4. 确认 iWcState=FALSE、iEthercatState 低 12 位为 8、bBusOk=TRUE、bFault=FALSE。
5. 在 Prepared value 中准备 bEnable=TRUE，执行 `PLC → Write values`。
6. 程序按状态字发送 6、7、15。等待 bReady=TRUE、iModeDisplay=8。
7. 使能前目标会对齐反馈，只使能时不应从任意零点突然走位。
8. 再写 bRun=TRUE，开始连续正转。
9. fCommandRpm 上升到 60，bAtOneRevPerSecond=TRUE，稳态 fIncrementCounts=2。

不要使用 Force values 强制 bEnable/bRun，也不要强制 qControlWord/qTargetPosition。
强制可能覆盖程序取消运行的逻辑。未 Ready 时的 bRun 会被清除，不排队等待启动。
CSP 稳态控制字保持 15，不使用 PP 的 47、63，不用一秒定时器去跳变位置。

## 6. 验证每秒一圈

稳定后程序每 500 个周期计算一次目标位置差：

| 监视变量 | 默认配置预期 | 含义 |
|---|---|---|
| fCommandRpm | 60.0 | PLC 轨迹速度，不是机械测速 |
| fIncrementCounts | 2.0 | 每周期指令增量，不是真实机械位移 |
| bAtOneRevPerSecond | TRUE | 指令达到一秒一圈，不证明机械达到 |
| bOneSecondSampleValid | TRUE | 有完整的稳态 500 周期样本 |
| nOneSecondTargetDelta | 1000 | 最近一个有效窗口的目标位置差 |

有效标志在第一完整稳态窗口后置 TRUE，之后保留到退出稳态。
减速、禁用、掉通信或模式未就绪会清除有效标志；此时位置差是旧样本，不应使用。
修改每圈计数后，差值预期也改变，例如每圈 1600 计数时应为 1600。
500 周期诊断不是独立墙钟计时，实际周期、任务超时和总线时序仍要单独检查。

用测速仪确认约 60 rpm，用轴上标记观察稳定运行 10 秒是否约转 10 圈。
当前固件 0x6064 是开环速度积分、0x606C 是应用速度估算，不证明没有丢步。
第一秒有加速，不严格一圈；从静止一秒内转一圈再停属于另一种定位轨迹。

## 7. 正常停止和再次启动

1. 写 bRun=FALSE，保持 bEnable=TRUE。
2. fCommandRpm 约 0.5 秒降到 0，目标停止递增并保持终点。
3. 等 bEstimatedStopped=TRUE，再按需要写 bEnable=FALSE。
4. bEstimatedStopped 只依据开环反馈，不是安全停机证明。
5. 保持使能、正常停稳后再次写 bRun=TRUE，从当前目标继续，不跳回零。

直接取消 bEnable 是撤销使能，不是上述正常减速。
不要把 qTargetPosition 改回 0 来停止；驱动器会把它解释为回到零位置。

## 8. 异常恢复与位置上界

通信失败或故障时，程序禁用并清除 bEnable、bRun，不自动续跑。
先排除故障，再写 bResetFault=TRUE；程序发送 0x0080，反馈故障消失后清除复位请求。
恢复后重新写 bEnable=TRUE，等 bReady，再写 bRun=TRUE。

接近 cPositionStopLimit 时 bPositionLimit=TRUE，清除 bRun 并正常减速。
本例只用于短时正转，不实现正负无限位置回绕；不能强制 bRun 绕过保护。
长时间无限运行需要先统一 MCU 和主站回绕约定，不能仅在 PLC 清零计数。

## 9. 跟不上时检查

- 实际 sxifen、cCountsPerRev、任务周期、总线同步任务和 WcState。
- iModeDisplay 持续为 8，qControlWord 稳态为 15。
- 当前固件 CSP 仍受 0x6081 限速，默认 100 rpm，应允许 60 rpm 及跟随修正余量。
- 固件 0x6083/0x6084 仍限制内部速度斜坡，默认 300 rpm/s，须允许轨迹变化。
- 指令为 60 rpm 而测速明显偏低时，检查实际丢步、负载和电机算法，不要修改估算反馈掩盖。

## 10. 自检与参考

本目录执行 `node verify_example.js`，检查源文件引用、默认配置和轨迹数值模型。
它不编译 ST，不验证 TwinCAT 运行时、总线定时或 MCU 算法；仍需 TwinCAT Build 和实机验证。

- [TwinCAT 任务周期](https://infosys.beckhoff.com/content/1033/tc3_system/5210416139.html)
- [EtherCAT 同步任务](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1842809099.html)
- [写值与强制值](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2527602315.html)
