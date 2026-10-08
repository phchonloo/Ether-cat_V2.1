# TwinCAT 3 CSV 单轴入门示例

CSV 是周期同步速度模式。主站每个周期发送 0x60FF 目标速度，0x6060=9。
驱动使能完成后，0x6040 保持 15。CSV 不使用 PP 的 47→63 触发。
当前固件把 0x60FF 解释为带符号 rpm：30 表示正转 30 rpm，-30 表示反转 30 rpm。
速度单位由本工程约定，不能直接套用到其他厂家驱动器。

本例每 2 ms 发送一次速度，PLC 生成加减速轨迹，默认加速度和减速度均为 60 rpm/s。
示例将速度限制为 +/-100 rpm；固件自身上限仍是 +/-1000 rpm。
本例尚未经过 TwinCAT 编译和电机实测。

## 1. 建立项目

1. 新建 TwinCAT XAE 测试工程，扫描当前驱动器，使用当前 SingleAxis Rev03 ESI。
2. 为这台从站保留唯一的输出控制源。若已经由 NC 或 CSP PLC 控制，先解除原输出绑定。
3. 第一轮沿用已经稳定的 SM 同步；跑通后再验证 DC。
4. 右键 PLC → Add New Item → Standard PLC Project，命名 CSV_Test。
5. 右键 GVLs 添加 Global Variable List，名称必须为 GVL_CSV。
6. 把 GVL_CSV.st 全部复制进去。
7. 打开 POUs → MAIN；上方声明区复制 MAIN_Declaration.st，下方程序区复制 MAIN_Implementation.st。
8. 不要在同一从站上同时运行并绑定本 CSV 示例与前面的 CSP 示例。

## 2. 设置任务并编译

1. 打开 SYSTEM → Tasks → PlcTask，调整 Cycle ticks，使实际周期显示 2.000 ms。
2. 基准 1 ms 对应 2 ticks；基准 100 us 对应 20 ticks。
3. 确认 MAIN 被该任务调用。
4. cTaskSec=0.002 必须与实际任务周期相同。
5. 右键 PLC 项目执行 Build。

## 3. 绑定 PDO

在从站的 Inputs/Outputs 中逐项右键 Change Link，绑定 PLC 实例下面的变量。
旧 ESI 可能还在 Module 1 下面，新版 ESI 在从站直接下面。

| 从站变量 | 索引 | PLC 变量 | 类型 |
|---|---|---|---|
| Outputs / Control Word | 0x6040 | GVL_CSV.qControlWord | UINT |
| Outputs / TargetPosition | 0x607A | GVL_CSV.qTargetPosition | DINT |
| Outputs / TargetVelocity | 0x60FF | GVL_CSV.qTargetVelocity | DINT |
| Outputs / ModeOfOperation | 0x6060 | GVL_CSV.qMode | SINT |
| Inputs / Status Word | 0x6041 | GVL_CSV.iStatusWord | UINT |
| Inputs / ActualPosition | 0x6064 | GVL_CSV.iActualPosition | DINT |
| Inputs / ActualVelocity | 0x606C | GVL_CSV.iActualVelocity | DINT |
| Inputs / ModeOfOperationDisplay | 0x6061 | GVL_CSV.iModeDisplay | SINT |
| 从站 WcState | 诊断 | GVL_CSV.iWcState | BOOL |
| 从站 InfoData / State | 诊断 | GVL_CSV.iEthercatState | UINT |

不要绑定主站的 Frm0Ctrl/DevCtrl。输出已有绑定时，取消 Only Unused 只能显示它，必须解除原绑定才能避免 Mapping conflict。

## 4. 检查周期通信并启动

1. Device 3 → EtherCAT → Advanced Settings → Cyclic Frames → Sync Tasks，确认 PlcTask 为 2000 us。
2. TwinCAT → Activate Configuration，确认 TwinCAT Run。
3. PLC → Login，按提示下载，再 PLC → Start。
4. 打开 GVL_CSV 在线监视，确认 iWcState=FALSE、iEthercatState 低 12 位为 8、bBusOk=TRUE、bFault=FALSE。

## 5. 零速使能

在 Prepared value 中准备新值，再执行 PLC → Write values。
操作 bEnable 和 nVelocityRequestRpm；不要直接强制 qControlWord/qMode/qTargetVelocity。

1. 写 bEnable=TRUE。
2. 程序根据状态字发送 6、7、15，并等待模式显示为 9。
3. 等到 bReady=TRUE、iModeDisplay=9、qControlWord=15。
4. 此时 nVelocityRequestRpm=0，电机应保持零速。

## 6. 正转、调速、停转、反转

1. 写 nVelocityRequestRpm=30，目标速度会逐渐升到 30，电机正转。
2. 默认 60 rpm/s，PLC 的 0→30 rpm 斜坡约需 0.5 s。
3. 稳定后写 60，电机加速到正转 60 rpm。
4. 写 0，电机减速到零，qControlWord 保持 15，模式仍为 9。
5. 待 iActualVelocity=0，再写 -30，电机反转。
6. 结束测试先写 0，待停稳后再写 bEnable=FALSE。

CSV 在非零速度目标保持时会持续转动，不会按一圈或某个目标位置自动停止。
0x607A 在当前 CSV 控制分支不决定运动方向或停点。

若通信中断或故障，程序清除使能和旧速度请求。恢复后重新使能、重新写速度。
故障解除后可以写 bResetFault=TRUE，然后重新使能。

## 7. 加减速

fAccelRpmPerSec 控制 PLC 提高速度的斜坡，fDecelRpmPerSec 控制降速和换向减速斜坡。
例如 0→30 rpm：60 rpm/s 对应约 0.5 s，30 rpm/s 对应约 1 s。
非正值在示例中按最低 1 rpm/s 处理。

当前 MCU 在 CSV 下也使用 0x6083/0x6084 做内部速度斜坡限制，默认均为 300 rpm/s。
这属于本工程的实现，不应认为所有 CSV 驱动器都这样使用这两个对象。
PLC 设定的加速度超过内部限制时，电机侧指令不能按 PLC 的较快斜坡达到目标。
当前 CSV 分支不使用 0x6081 作为速度上限；它使用固件的 +/-1000 rpm 限制。

## 8. 验证基本功能

确认模式显示 9，控制字 15，并检查：

1. 请求 30 时电机正转，请求 -30 时反转。
2. 请求 60 时实测转速高于 30，请求 0 时停稳。
3. 只改变目标位置不会使当前 CSV 改变运动。
4. 稳态持续转动、重复调速及正反转换正常。
5. 用测速仪测真实转速。0x606C 由施加的电机指令计算，不能当成独立传感器反馈。

## 9. DC

单轴测试完成后，先零速停稳并撤销使能，再选择从站 DC-Synchron。
Sync0、PlcTask 和对应 EtherCAT 任务保持 2 ms，主站 Dc Sync Task 对应 PlcTask。
重新激活、Login、Start，确认 OP 和 WcState=0、PDO 更新正常，再使能和设置速度。

## 官方参考

- [CSV 工作方式](https://doc.synapticon.com/actilink_s/sw5.4/motion_control/operation_modes/cyclic_modes/csv.html)
- [写值与强制值](https://infosys.beckhoff.com/content/1033/tc3_plc_intro/2527602315.html)
- [EtherCAT 同步任务](https://infosys.beckhoff.com/content/1033/tc3_io_intro/1842809099.html)
