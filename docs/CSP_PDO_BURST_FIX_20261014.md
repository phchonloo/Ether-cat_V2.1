# CSP PDO 重触发修正与停机证据（2026-10-14）

适用：当前 FCM4E1553/N32G455 固件，以及用户确认正在运行的 `GVL_CSP_NEW`（NewProject）。本次不改 ESI、PDO 布局、同步周期、中断优先级或电机参数，不自动下载、启用或运行设备。

## 分析中已确认与尚未确认的部分

NewProject 源码确实在 `iWcState=TRUE` 的当前扫描置停止原因位 1。若已有使能／运行请求，会锁存停机、清除请求并把输出控制字清零。通信恢复不会自动解锁。因此 MCU 收到控制字 0，可能是 PLC 通信保护的结果，而不是 MCU 自发故障。

用户提供的外部分析报告称：约 73 分钟出现 142 次重触发，PDO 间隔约 4.4 ms。本次没有取得该轮原始诊断或抓包，不能把这些数据写成独立复测结论。

- `SystemCoreClock=144000000` 表示 CPU 计时频率 144 MHz。633600 个周期 / 144000000 = 0.0044 秒，即 4.4 ms。
- 原 `maxEnabledPdoGapCycles` 是运行期间两次成功 PDO 映射的历史最大间隔，不一定对应本次停机，也不是线上以太网帧到达时间。
- `outputRetriggerCount` 是 ISR 处理后仍检测到输出事件的次数；ESC 事件和缓冲区不是逐帧队列，142 不能直接等同于准确的两帧并发次数或丢帧数量。
- WcState 无效说明本扫描的过程数据通信无效，不能单独区分主站未发送、返回延迟、链路问题或从站处理时序问题。

参考：[Beckhoff WcState](https://infosys.beckhoff.com/content/1033/b110_ethercat_optioninterface/1984417163.html)、[主站帧诊断](https://infosys.beckhoff.com/content/1033/tcsystemmanager/1089009035.html)。

## MCU 修改

原 `PDI_Isr()` 已映射一份输出后，若复查又看到输出事件，只读 SM 首尾地址应答，没有把第二份完整 PDO 交给应用层。第二份可能包含新的目标或撤使能命令。

现在在 OP 输出已启动的条件下完整读取并映射第二份 PDO；SM 同步模式执行相应应用更新，DC 模式仍沿用同步事件处理，不因为两份 PDO 而人工补发两个 SYNC0。SAFEOP 仍只应答，不执行输出。

- 每次 ISR 最多两份完整 PDO，最多三次 AL 事件快照（含原有晚到首份分支），无新增无上限循环。
- 保留 `u16CycleExceededCounter` 超周期告警；第二份复制不再额外扣减丢失事件计数，以免突发数据冲淡同步故障证据。
- 第二份读取失败时，沿用原有原子发布和 PDI 故障锁存，不把部分数据或补零内容解释成有效指令。
- 该修改不是逐帧缓存，也不保证任意长突发均可处理。持续重触发、持续低电平 IRQ 或同步错误仍须排查，不能不断增加 ISR 循环次数。
- 完整读取第二份 PDO 会增加重触发路径的处理时间；离线回归不能给出实板最坏 IRQ 占用时间，须同时观察 SYNC0 响应和电机周期余量。

新增 `gEcatSyncDiag.outputRetriggerReadCount` 表示额外完整 PDO 读取的尝试次数，不是成功次数；读取失败看原有 `pdoReadErrorCount` / `gEscPdiDiag`。重触发相关新增计数采用饱和更新。

`gEcatCommDiag.diagVersion` 更新为 `0x20261014`，新增：

| 字段 | 含义 |
| --- | --- |
| lastPdoGapCycles / lastPdoGapValid | 最近两次成功映射的 CPU 周期间隔及有效性 |
| lastPdoControlWord | 最近成功映射的 PDO 控制字 |
| firstDisableLastPdoGapCycles / firstDisableLastPdoGapValid | 首次撤使能时冻结的最近映射间隔 |
| firstDisableLastPdoCycle | 同时冻结的最近 PDO 映射时间戳 |
| firstDisableLastPdoControlWord | 同时冻结的最近 PDO 控制字 |

首次撤使能记录用短暂、仅复制 RAM 的临界区保持字段一致，随后恢复调用前的 PRIMASK。它记录撤使能时最后一份成功 PDO，不保证就是最初下达停机命令的那一帧。旧最大值仍保留，便于比较“历史最大”和“最近一次”。

DWT 未启用或刚启用的首个样本无有效间隔。32 位计数会回绕；在 144 MHz 下完整回绕约 29.8 秒。中断调试、长暂停、复位、时钟变化会影响解释；不能用这些诊断替代线上时间戳。失败读取不更新成功样本。

## PLC 修改与保护边界

NewProject 1.1 增加连续无效扫描数、最大连续数、异常段数、最近结束段长度，以及 `diagIssuedControlWord`。原 `diagControlWord` 仍保存保护前的输出，新增字段保存同次保护后的输出，从而明确 15 → 0 的 PLC 行为。

保持单次 WcState 异常立即停机、目标停止推进、故障锁存和人工恢复。没有修改为“忽略一帧”或自动续跑。软件调整不能把当前 demo-only 网卡变成实时网卡，所以仍可能因真实 WcState 异常停机。

若目标是减少主站造成的停机，应使用已确认 realtime capable 的主站配置作同条件对照；不要靠抹掉 WcState、拉大同步／看门狗阈值来验收。

## 更新与验证

1. 保存当前故障首错记录，在安全停机后由用户编译／下载新固件。工具没有执行烧录。
2. 更新 NewProject 的 `GVL_CSP_NEW.st` 和 MAIN 实现；MAIN 声明未新增变量。README 三个复制块与文件由回归核对。
3. 重新 Build PLC，确认原 PDO 绑定和 2 ms 任务未变。可选主站诊断只有实际绑定后才能声明有效。
4. 先检查 `diagVersion=0x20261014`，手动按原步骤复位／使能／运行。通信恢复不会触发自动启动。
5. 若再次停止，保存 PLC `nStopReason`、`diagControlWord`、`diagIssuedControlWord`、`nLastWcEpisodeCycles`、`nWcBadStreakMax`，以及 MCU 首次撤使能与同步首错记录。不要先断电清除证据。

离线回归入口：

```text
node tests/verify_ethercat_sync.js
node examples/TwinCAT_CSP_NewProject/verify_example.js
D:\tool\Python\Python39\python.exe tests/verify_pdi_faults.py
D:\tool\Python\Python39\python.exe tests/verify_pulse_position.py
```

ARM 测试执行重建后的 AXF，硬件访问用夹具模拟；包含第二份控制字为 0 和第二份读取失败。ST 测试执行受限语法回放，不是 TwinCAT 编译器。它们不能测量真实 IRQ 延迟、网卡抖动、实板电气信号或证明长时间运行不再停机。4.4 ms 用例是人工注入测试值，不是硬件复测。

## 本次验证结果

- Keil N32G455 完整 Rebuild：0 errors / 10 warnings。
- 同步事件源码回放：25 项通过。
- NewProject ST 回放：22 项基础 + 12 项新增，共 34 项通过。
- 新 AXF 原生 ARM 故障注入／配置回归：27 项通过。
- 完整脉冲位置、有限 PP 目标保持、CSP 轨迹／停机、PLC 与 ARM 联合回放及 CSV 回归全部通过。
- 构建前后 `Project.uvoptx` SHA-256 一致，用户调试设置未改动。
- 未使用 TwinCAT 编译器或真实网卡／电机测试，未执行下载、复位或使能。

构建日志：`tmp/pdo_burst_fix_20261014_build.log`。

固件：`program/FCM4E1553 Ethercat/MDK-ARM/N32G455/N32G455.axf`。

固件 SHA-256：`032754a16f8c7f4be3809541e35552f1c095d55cd7af149cbabd7b1906c34afa`。

本次改动不等同于“主站丢帧问题已解决”。验收仍需实板同条件长时间运行，并核对 WcState、重触发统计、SM/SYNC 首错和实际电机响应。
