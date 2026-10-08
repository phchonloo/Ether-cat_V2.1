# TwinCAT 3 CSP 单轴入门示例

本例由 PLC 直接写 EtherCAT PDO，使用当前工程的 `0x6060=8` CSP 模式。
在一个 2 ms 周期的 PlcTask 中，PLC 生成连续位置轨迹，让电机在 4 秒内平滑正转一圈。
之后把位移改成负数，可以反转一圈。PLC 保持 `0x6040=15`，CSP 不使用 PP 的 `47→63` 触发。

当前 `User/SaveFunc.c` 的默认 `sxifen=1000`，因此默认一圈对应 1000 个位置计数。
若实际运行参数修改了 sxifen，应按实际每圈计数修改位移。
`0x6064` 来自开环速度积分，表示估算位置；它不能证明机械轴没有丢步。

这三个 `.st` 文件是供复制到 TwinCAT 编辑器的源代码，尚未在 TwinCAT 编译器或实机上验证。

## 1. 准备测试工程

1. 在 TwinCAT XAE 中新建一个测试工程，扫描一台当前驱动器。
2. 使用工程根目录的 `FCM4E1553_CiA402_SingleAxis_Rev03.xml` 对应的 ESI。
3. 本例不需要 NC 轴。若复用已有工程，先解除这个从站控制字、目标位置、目标速度及模式的 NC 输出绑定。
4. 第一轮使用已经验证稳定的 SM 同步配置，先确认 PLC 与 PDO 正常。DC 的配置见第 9 节。

## 2. 添加 PLC 工程和 GVL

1. 右键 `PLC` → `Add New Item...`。
2. 选择 `Standard PLC Project`，命名 `CSP_Test`。
3. 展开 `CSP_Test Project`，找到 `GVLs`。
4. 右键 `GVLs` → `Add` → `Global Variable List...`，名称必须是 `GVL_CSP`。
5. 将 `GVL_CSP.st` 的全部内容复制到新 GVL 中。
6. 打开 `POUs → MAIN`。
7. 把 `MAIN_Declaration.st` 放在 MAIN 上方声明区，替换空模板声明。
8. 把 `MAIN_Implementation.st` 放在 MAIN 下方 ST 程序区。

这里的 `%Q*` 表示 PLC 发往从站，`%I*` 表示从站返回 PLC。
无 AT 声明的 bEnable 等变量是 PLC 内部操作按钮，不绑定 PDO。

## 3. 设置 2 ms 任务

1. 在 `SYSTEM → Tasks → PlcTask` 打开任务配置；也可双击 PLC 工程中的 PlcTask 任务引用。
2. 修改 `Cycle ticks`，使右边显示的实际周期为 **2.000 ms**。
3. 如果基准时间为 1 ms，则为 2 ticks；如果基准时间为 100 μs，则为 20 ticks。
4. 确认 MAIN 被该 PlcTask 调用；标准 PLC 模板通常已经包含这个调用。
5. 示例 `cTaskSec=0.002` 必须与任务周期相同。更改任务周期时同步修改该常量。

## 4. 编译并绑定变量

先右键 PLC 项目 → `Build`。编译成功后，PLC 实例下面会生成 PlcTask 的 Inputs/Outputs。

找到实际从站的 Inputs/Outputs。有旧 ESI 时它们可能在 Module 1 下面；新版 ESI 在从站直接下面。
逐项右键 I/O 变量 → `Change Link...`，选择对应 PLC 实例变量。

| 从站变量 | 索引/位置 | PLC 变量 | 类型 |
|---|---|---|---|
| Outputs / Control Word | 0x6040 | GVL_CSP.qControlWord | UINT |
| Outputs / TargetPosition | 0x607A | GVL_CSP.qTargetPosition | DINT |
| Outputs / TargetVelocity | 0x60FF | GVL_CSP.qTargetVelocity | DINT |
| Outputs / ModeOfOperation | 0x6060 | GVL_CSP.qMode | SINT |
| Inputs / Status Word | 0x6041 | GVL_CSP.iStatusWord | UINT |
| Inputs / ActualPosition | 0x6064 | GVL_CSP.iActualPosition | DINT |
| Inputs / ActualVelocity | 0x606C | GVL_CSP.iActualVelocity | DINT |
| Inputs / ModeOfOperationDisplay | 0x6061 | GVL_CSP.iModeDisplay | SINT |
| WcState | 从站诊断输入 | GVL_CSP.iWcState | BOOL |
| InfoData / State | 从站状态输入 | GVL_CSP.iEthercatState | UINT |

不要绑定 Device 3 自身的 Frm0Ctrl、DevCtrl 等主站控制变量。
WcState 和 InfoData/State 必须选本驱动器的节点。
如果出现 Mapping conflict，解除原输出绑定后再绑定；取消 Only Unused 只是显示已有绑定，并不消除冲突。

## 5. 检查 EtherCAT 同步任务

1. 打开 `Device 3 (EtherCAT) → EtherCAT → Advanced Settings...`。
2. 找到 `Cyclic Frames → Sync Tasks`。
3. 应能看到 PlcTask，周期 2000 μs。绑定 PLC PDO 后会建立任务关系。
4. 若任务关系不正确，回到 `EtherCAT → Sync Unit Assignment...` 检查该从站所属同步单元与 PlcTask 的关系。

## 6. 激活和运行

1. `TwinCAT → Activate Configuration`，确认重新启动 TwinCAT 到 Run。
2. 选中 PLC 工程执行 `PLC → Login`，需要下载时下载。
3. 执行 `PLC → Start`。
4. 打开 GVL_CSP 在线监视。TwinCAT Run 与 PLC Start 是两个独立状态，两者都要正确。
5. 确认 iWcState=FALSE、iEthercatState 低 12 位为 8、bBusOk=TRUE。
6. 此时 bEnable 默认 FALSE，电机不会执行示例运动。

## 7. 使能，再正转一圈

操作变量时：在在线声明区的 `Prepared value` 中输入新值，然后执行 `PLC → Write values`。
本例使用一次写入；不要对 bStartMove 做 Force values，否则可能反复触发。

1. 写 bEnable=TRUE。
2. 程序按状态字自动发送 6、7、15。已处于某个启动状态的从站会从相应步骤继续。
3. 等待 bReady=TRUE、iModeDisplay=8，状态字与 0x006F 按位与后的值为 0x0027。
4. 在使能前程序已将目标位置对齐反馈，因此只使能时应不走位。
5. 保持 nMoveCounts=1000、fMoveTimeSec=4.0，写 bStartMove=TRUE。
6. bStartMove 会自动回到 FALSE，bBusy 变为 TRUE，qTargetPosition 在 4 秒内连续增加 1000。
7. 轨迹发完后 bTrajectoryDone=TRUE，目标保持在终点；待驱动停稳后 bEstimatedAtTarget=TRUE。
8. bTrajectoryDone 只表示 PLC 轨迹发完；bEstimatedAtTarget 也只依据开环反馈判断。
9. 若 bRequestRejected=TRUE，说明驱动尚未停稳、正在运动或目标超出 DINT 范围，等待停稳后重新发请求。

程序在通信失败或故障后撤销 bEnable，通信恢复后要重新写 bEnable=TRUE。
如果 bFault=TRUE，先排除故障，再写 bResetFault=TRUE；故障清除后重新使能。

## 8. 反转和调速

停稳后写 nMoveCounts=-1000，再写 bStartMove=TRUE，会从当前保持位置反转一圈。
nMoveCounts 是本次相对位移；PLC 转成绝对目标发送，所以 CSP 不需要设置控制字的相对定位 bit 6。

同一位移下，fMoveTimeSec 越小，速度越高。示例最小接受 1 秒，第一次验证保持 4 秒。
本例采用三次平滑曲线：一圈 4 秒的平均速度为 15 rpm，峰值为 22.5 rpm；一圈 8 秒分别为 7.5 和 11.25 rpm。
峰值速度公式是 `90 × |位移计数| / (每圈计数 × 时间秒)`。

当前固件仍用 0x6081 限制 CSP 速度，默认 100 rpm；0x6083/0x6084 是内部速度斜坡限制。
PLC 轨迹超过这些限制时，驱动会跟不上目标。逐步提速时要同时核对限制和真实电机表现。
qTargetVelocity 在此 CSP 示例中固定为 0，运动速度由连续位置轨迹决定。

## 9. 单轴跑通后验证 DC

1. 电机停稳，写 bEnable=FALSE。
2. 打开从站 DC 页，选择 ESI 提供的 DC-Synchron 模式。
3. Sync0 周期与 PlcTask 保持一致，使用 2 ms；当前 ESI Factor=1 跟随对应任务周期。
4. 在 EtherCAT 主站 DC 配置中检查 Dc Sync Task 对应 PlcTask。
5. 重新 Activate Configuration、Login、Start。
6. 确认 OP、WcState=0、PDO 持续更新，再按第 7 节使能和运动。
7. 若出现 0x001A Synchronization error、掉 SAFEOP 或 PDO 不更新，先停止运动检查同步，不能仅凭 OP 判断 DC 已正常。

## 官方操作参考

- [TwinCAT 标准 PLC 工程](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2526130571.html)
- [TwinCAT PLC 入门](https://infosys.beckhoff.com/content/1033/tc3_system/2525041803.html)
- [Task 周期](https://infosys.beckhoff.com/content/1033/tc3_system/5210416139.html)
- [EtherCAT Sync Tasks](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1842809099.html)
- [写值与强制值](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2527602315.html)
- [WcState](https://infosys.beckhoff.com/content/1033/b110_ethercat_optioninterface/1984417163.html)
