# PP 持续转、CSP 偶发停机：本次修正与下一步

日期：2026-10-13。工程：`program/FCM4E1553 Ethercat/MDK-ARM/Project.uvprojx`，目标 N32G455。

## 1. 现在可以确定什么

本次用户反馈：

- PP 未运行相同的 PLC，而是在 Box 1 中逐项给值；给一个位置后持续转动。
- CSP 首错仍是 `nStopReason=1`、`diagWcState=TRUE`。
- `diagMasterDiagValid=FALSE`，三个主站快照值都是 0。

结论是：CSP 的直接停机链路仍然是 **过程数据无效 → PLC 锁存停止 → 撤掉控制字使能 → MCU 停止**。需要继续区分的是过程数据为什么无效，不是取消这条保护。

`diagMasterDiagValid=FALSE` 表示该次首错没有有效的主站帧级快照。后面三个 0 不能证明主站正常；之后再绑定也不会补回已经发生的故障。

PP 与 CSP 这两次测试的控制来源、运动命令和停机判据不同，不能把它们当成同条件对照。PP 在驱动内部执行一个被接受的有限目标；CSP 的轨迹目标由主站周期提供。这不表示 PP 可以无视 EtherCAT 看门狗，也不表示 CSP 天生会随机停机。[模式差异参考（厂商 EPOS4 文档，仅用于原理，不套用其专属参数）](https://support.maxongroup.com/hc/en-us/articles/4402184834322-EPOS4-IDX-CSP-or-CSV-Mode-Relevance-of-Interpolation-Time-Period-Value)

## 2. 本次已确认并修正的软件缺陷

### 2.1 OP 退出记录会漏记

SSC 的 `AL_ControlInd()` 中有先给 `nAlStatus` 赋新状态、再调用 `SetALStatus()` 的路径。旧记录代码却在 `SetALStatus()` 中比较 `nAlStatus` 和新状态，两者已经相等，因而跳过首个退出 OP 的记录。

已在修改前用原 AXF 的 ARM 代码重现：预先赋值后 `firstOpExitValid` 仍为 0，新增回归断言失败。

修正后比较“上一回发布的 AL 状态请求”和本次请求；第一次调用尚无历史时才使用当前 `nAlStatus`。不改变 SSC 的状态转换、SM 使能顺序、AL 写入顺序或同步保护。记录仍是软件请求的证据，不是 ESC 已成功接受寄存器写入的证明。

因此，旧固件截图里的 `firstOpExitValid=0` **不能用来排除短暂退出 OP**。它也不能反过来证明本次一定退出了 OP；需要修正版的新记录。

### 2.2 区分全程最大 PDO 间隔与使能采样期间的最大间隔

原 `maxPdoGapCycles` 包含启动、主动停用、调试暂停等间隔，不一定是故障发生时的间隔。本版保留原字段，增加：

| `gEcatCommDiag` 字段 | 含义 |
| --- | --- |
| `diagVersion` | 本版为十六进制 `0x20261013`，用于确认诊断源码/AXF 的版本 |
| `lastAlStateValid` | 已有上一回 AL 发布请求，供诊断内部使用 |
| `firstOpExitCycle` | 首次退出 OP／进入 OP 错误状态时的 DWT 计数 |
| `firstOpExitPdoCount` | 该时刻成功输出 PDO 处理累计数 |
| `maxEnabledPdoGapCycles` | 相邻成功 PDO 处理时刻均采到电机使能时的最大间隔 |
| `firstDisableCycle` | 首次由 AL/CiA402 状态门控撤销电机使能的 DWT 时刻 |
| `firstDisablePdoCount` | 该时刻成功输出 PDO 处理累计数 |
| `firstDisableMaxEnabledPdoGapCycles` | 该时刻冻结的上述间隔最大值，不是停机前一帧的单独间隔 |

这些字段都不产生新故障，也不放宽旧保护。首错保持到 MCU 重新初始化；新值不会自动覆盖首错。

间隔是 **MCU 处理成功 PDO 的间隔**，不是主站发送周期、链路传播时间或原始网包时间戳。相邻采样使能不证明中间一直使能。DWT 必须启用；32 位计数会回绕，长时间暂停不能据此计算精确时长。换算微秒：`cycles * 1000000 / SystemCoreClock`。若 CPU 为 144 MHz，288000 周期约为 2 ms；不要在其他频率下照搬。

本次没有改 PDO/ESI、目标轨迹、速度限制、SQI 分频、通信/同步看门狗，也没有自动下载或启动电机。这是已确认诊断缺陷的修正，不是“随机停转根因已全部修好”的承诺。

## 3. 先完成主站诊断绑定

使用新示例 `examples/TwinCAT_CSP_NewProject` 的 `GVL_CSP_NEW`；如果仍用另一份 GVL，请不要把名字混用或同时让两个程序写同一驱动器的 PDO。

先让 `bRun=FALSE`、`bEnable=FALSE`，确认机械安全静止，保存已有首错。需要重新激活配置时应保持驱动不使能。

在 Solution Explorer 展开 **I/O → Devices → Device 3 (EtherCAT) → Inputs**，不是 Box 1 → Inputs。主站的实际名称可能不同。

| 主站输入节点 | 绑定到 PLC 输入变量 |
| --- | --- |
| `DevState` | `GVL_CSP_NEW.iMasterDevState` |
| `Frm0WcState` | `GVL_CSP_NEW.iMasterFrameWcState` |
| `Frm0State` | `GVL_CSP_NEW.iMasterFrameState` |

双击对应主站变量，在变量链接界面选择上表 PLC 变量；检查 `Linked to` 确实对应。选择包含本驱动器过程数据命令的帧，**不一定是 Frame 0**。可以在主站 EtherCAT 页查看周期帧和命令的分配。多帧时不能随意绑定别的帧。

完成 Build、映射和所需的配置激活后，登录 PLC。在确认三项正确映射到同一 PlcTask 的输入后，将实时配置 `bMasterDiagLinked` 写为 TRUE。

- `bMasterDiagLinked` 是运行前的配置开关。
- `diagMasterDiagValid` 是发生首错时保存的有效标志；不是绑定状态的自动检测器，也不是运行前必然为 TRUE 的指示灯。
- 三个实时输入正常时可能全为 0，**不能凭全为 0 判断绑定正确**，必须检查链接。
- 若找不到主站 Inputs 或对应帧，保持开关 FALSE，发主站树截图；不要绑定相似名称的从站变量，不要把开关直接置 TRUE 冒充已绑定。

保存旧首错后，在通信正常、无 Fault、电机静止、`bEnable/bRun=FALSE` 时手动写一次 `bResetDiag=TRUE`。开始新测试前确认旧锁存已解除。新一轮故障才会捕获有效主站快照。不要在未保存证据时复位或重启。

首错的基本分支如下，可组合出现，不能机械地归为唯一原因：

| 有效主站首错证据 | 下一步方向 |
| --- | --- |
| `diagMasterFrameState AND 16#8000` 非 0 | 主站记录整帧未发送；查任务/实时调度、发送资源及网卡驱动 |
| `diagMasterFrameWcState AND 16#8000` 非 0 | 整帧未收到；结合主站计数增量、链接状态、从站记录区分发送/接收时序和链路问题 |
| `diagMasterFrameWcState AND 16#7FFF` 非 0 | 命令收到但工作计数不符合预期；查对应命令、从站 AL/SM/FMMU 状态及通信链路 |
| MCU 新记录 `firstOpExitValid=1` | 看 `firstOpExitCode`、同步首错、PDI 首错；区分主动状态切换、看门狗和同步异常 |
| MCU `gEscPdiDiag` 出现新错误 | 按 PDI 首错类型和地址检查底层访问 |
| 主站快照有效但三项都是 0，PLC WcState 异常 | 先核实帧选择、任务映射和采样关系，不能据此宣布总线无故障 |

快照采于 PLC 扫描与 MCU 的不同代码时刻，不能当作严格同步的抓包。[Beckhoff 主站/从站诊断位定义](https://infosys.beckhoff.com/content/1033/tcsystemmanager/1089009035.html)

## 4. PP 单次给位置却持续转动，如何检查

本工程 PP 路径接受有限位置后应到位并保持；不是“只要使能就无限转”的模式。实际测试前先检查以下对象，不要根据曾经写过什么推断当前值：

| 对象 | 应确认的问题 |
| --- | --- |
| `6060 / 6061` | 请求模式和实际模式是否都为 1？实际为 3/9 时是速度模式，不是 PP |
| `607A / 6064` | 目标和实际位置是多少、单位与进制是什么？绝对位置差是否远大于一圈？ |
| `6040 / 6041` | 新目标握手和实际状态是否正确？不是只设置目标就算已接受 |
| `606C` | 反馈速度是否为 0？本工程反馈来自内部相位计数，不能等同机械编码器 |

用户说明是在 Box 1 中逐项给值，但尚未取得这些实际值，所以目前不能确定 PP 持续旋转的具体原因。

区分写入位置：

- **CoE Online** 是 SDO 对象访问。若相同对象同时被 RxPDO 周期映射，固件的 `APPL_OutputMapping()` 会在新 PDO 到来时按 PDO 值更新控制字、模式和目标。一次手动 SDO 写入可能被覆盖。
- **Outputs → Online/Write** 写的是主站输出过程映像。若该变量又链接到运行的 PLC/NC，下一次程序更新仍可能覆盖手动值。

不要混用 SDO、手动输出、PLC、NC 同时控制一个对象。不要在运动中逐项改模式或强制值。保存运行后的实际值即可，无需为了截图再次让电机旋转。

当前离线测试已有 PP 有限目标 +1000、反向 -1；本次补充到位后控制字保持 63 的持续观察，检查是否重复接受同一个目标。离线通过只能验证内部相位命令计数，不证明实物转子一定正确跟随。

## 5. 如何做有效的对照，不再盲目换代码

先补齐诊断，而不是马上更改通信周期、滤波、保护或速度。按一次只改变一个条件进行：

1. 新固件正常运行，不暂停 MCU、不单步、不插运动断点；保持当前 PLC/总线配置和低速参数。
2. 电机不使能，仅保持总线正常通信，观察 `nBadWcCycles` 是否增长。增长支持故障不依赖实际转动；未增长不能排除低概率异常。
3. 在安全台架上分别记录使能保持与低速 CSP 的首错和计数增量，测试时间至少覆盖以往最长出现故障的时段。不同运动条件不能据一次“未发生”定论。
4. 当前网卡截图属于 `for demo use only`，这是已知实时环境限制。若有明确显示实时能力可用的测试电脑/网卡，可保持同一固件、周期、线缆和运动参数做替换对照。一次成功不证明原网卡就是唯一根因。

不要执行：屏蔽 WcState、延时忽略错误、自动清故障/重启运动、关闭同步看门狗、在运动中断点抓值或未经确认安装网卡驱动。本次助手不会执行这些操作。

## 6. 编译、验证与交付边界

由用户在机械安全静止且 PLC 不使能的条件下，手动 Rebuild、下载本工程，正常运行后检查 `gEcatCommDiag.diagVersion=0x20261013`。同时确认 Keil 使用的 AXF 与下载固件匹配。助手只做离线构建，不下载、不重启控制器、不激活 TwinCAT 配置。

离线验证项目：

```text
python tests/verify_pdi_faults.py
node tests/verify_ethercat_sync.js
python tests/verify_pulse_position.py
node examples/TwinCAT_CSP_NewProject/verify_example.js
```

实际结果在本文件末尾记录。软件测试不能验证网卡实时性、实际 WKC、SQI 电气时序、机械位置精度或长期稳定性。本轮真实停机根因仍需有效主站快照与修正版 MCU 记录来定位，不能将诊断补丁说成已经解决所有停转问题。

### 本次实际验证记录

| 项目 | 2026-10-13 结果 |
| --- | --- |
| 修改前复现 | 原 AXF 在预先赋值路径漏记 OP 退出，新增断言失败 |
| 修改后 N32G455 构建 | 0 错误，10 项既有警告（SaveFunc 未使用变量、eeprom_console 串口函数隐式声明）；不代表零警告 |
| ARM 通信故障注入 | 22 项通过，包含真实 `AL_ControlInd` 路径、DWT 回绕和停机证据保持；硬件接口模拟 |
| 同步事件回放 | 22 项通过，通信/同步看门狗仍然有效 |
| 脉冲／定位与 PLC 桥接 | 全部通过，包含 PP 单目标到位后保持 63 的 1024 个电机周期、CSP 轨迹、暂停、CSV 和故障关断；不是实物编码器测试 |
| 新 CSP PLC 源码回放 | 22 项基础及 7 项新增检查通过，共 29 项；未做 TwinCAT 编译 |
| 用户文件保护 | 构建前后 Project.uvoptx 的 SHA256 一致，没有覆盖用户调试设置 |
| 真实设备操作 | 未下载、未重启、未控制电机、未改变网卡配置 |

构建日志保存在 `tmp/csp_diag_20261013_build.log`。需要实板长时间运行验证后，才能判断真实停机原因及修复效果。
