# IRQ 边沿保留与持续低电平恢复（2026-10-15）

适用：当前 FCM4E1553 / N32G455 工程。参考用户提供的
`01_FCM4E1553_SQI_82_CTT_V1.4 (2)` 中的中断清标志及软件补触发方法，
合并到当前已经支持第二份 PDO 完整读取的版本。

## 实现

### IRQ / SYNC0 提前清 EXTI 标志

`EXTI0_IRQHandler` 先清本次 EXTI0 标志，再调用 `PDI_Isr()`。
退出时不再清 EXTI0，避免清掉服务期间新到来的下降沿。
`EXTI1_IRQHandler` 同样在 `Sync0_Isr()` 前清 EXTI1。

IRQ 入口只调用一次 `PDI_Isr()`。该函数已有 AL 事件复查和第二份 PDO
完整读取，每次硬件 IRQ 最多两次完整 RxPDO 读取尝试。没有叠加参考版本
的三轮外层循环，也没有在 IRQ 末尾因引脚低而反复软件触发自身。
处理期间新产生的硬件边沿仍可自然形成后续中断；上述限制是单次入口的
软件处理量上限，不是对任意硬件中断风暴的 CPU 占用保证。

保留当前第二份 PDO 的完整发布，包括控制字 15 → 0 的停止命令；
保留读取失败锁存、SAFEOP 不应用输出、超周期诊断和 SYNC 监控。

### 定时器补触发

原 `ECAT_CheckTimer()` 仍先执行。随后新增检查同时满足：

1. 自上一次定时器检查以来，`gEcatSyncDiag.pdiIsrCount` 没有变化；
2. EXTI0 的中断屏蔽位处于启用状态；
3. PB0 / ESC IRQ 为低电平；
4. EXTI0 没有待处理标志。

连续两次定时器检查满足条件时，通过 `EXTI_TriggerSWInt(EXTI_LINE0)`
请求一次正常 IRQ 服务，然后重新累计观察次数。已有待处理标志、IRQ
回高、EXTI0 被屏蔽，或有新的 PDI 服务进展，都会清除累计次数。
PDI 计数回绕也按计数变化处理。

这里的“两次”是协议栈定时器检查次数，不保证恰好等于 2 ms。实际间隔
受 TIM6 配置和调度影响。补救分支本身不调用 PDI，也不增加 SQI 读写；
原来的协议栈定时器行为保持原样。

补触发不清除 PDI / CiA402 故障，不强制恢复 OP，不复位 SM/DC 看门狗，
不写电机使能或控制字。原检查已经触发的故障仍按原逻辑处理。

## 与参考版本的取舍

- 采用入口提前清 IRQ / SYNC0 标志和定时器补触发的思路。
- 使用现有 PDI 内部复查，保持两份 PDO 的单次入口上限。
- 补触发增加“没有 PDI 进展”和“EXTI0 已启用”的条件，避免把持续有
  服务进展的低电平误当成停滞。
- 诊断计数饱和，不因计数溢出而显示回零。
- 保留当前最近 PDO 间隔、首次停机快照和 NewProject 1.1 的诊断。

本次没有修改 ESI、PDO 映射、PLC、运动参数、中断优先级或 SYNC1 屏蔽。
PLC 仍在一次 WcState 无效时锁存停机；通信恢复不自动启动电机。

## 新增 Watch 变量

在 Keil Watch 中展开 `gEscIrqDiag`：

| 字段 | 含义 |
| --- | --- |
| version | 固定为 `0x20261015`，用于确认本次 IRQ 修正版 |
| lowAfterServiceCount | IRQ 服务结束检查时 PB0 仍为低的次数 |
| recoveryRequestCount | 定时器发出的软件补触发请求次数 |
| maxServiceCycles | IRQ 入口内部测量区间的最大 DWT CPU 周期数 |

`lowAfterServiceCount` 不是丢帧数，也不单独证明丢中断：新的 PDO、邮箱或
其他未处理 ESC 事件都可能使线路仍为低。`recoveryRequestCount` 是请求
次数，不证明请求已被执行或通信已恢复。两个计数上限均为 `0xFFFFFFFF`。

`maxServiceCycles` 在 DWT 计数器入口、出口均启用时更新。144 MHz 下：

```text
测量区间微秒数 = maxServiceCycles / 144
```

测量区间不包含完整异常进入/退出和函数末尾开销，因此不能当成完整 ISR
最坏执行时间。计数支持一次正常模运算回绕；调试暂停、长时间停顿、时钟
变化会影响解释。需要真实时序时，应同时观察 IRQ、SYNC0 和执行标记 GPIO。

原 `gEcatCommDiag.diagVersion` 仍为 `0x20261014`，因为通信诊断结构未改；
不要把它与新的 IRQ 诊断版本混淆。

## 验证

本次修改的源码：

- `program/FCM4E1553 Ethercat/Ethercat/port/el9800hw.c`
- `program/FCM4E1553 Ethercat/Ethercat/Inc/el9800hw.h`
- `tests/verify_pdi_faults.py`

Keil N32G455 完整 Rebuild：0 errors / 10 warnings。
10 条告警与上一份构建清单一致，来自既有的 EEPROM 串口函数声明和
SaveFunc 未使用变量。本次未增加编译告警。

- 同步事件源码回放：25 项通过。
- NewProject PLC 受限 ST 回放：34 项通过。
- 新 AXF 的 ARM 故障注入/配置回归：37 项通过，其中新增 10 项覆盖实际
  EXTI0、EXTI1、TIM6 编译入口的边沿、补触发和有界处理行为。
- 电机脉冲位置、有限 PP 目标保持、CSP 轨迹/保护、PLC 与 ARM 联合回放、
  CSV 回归全部通过。

ARM 回归使用真实编译函数、模拟 GPIO/EXTI/ESC 寄存器行为；它不模拟完整
NVIC 抢占，也不验证实板延迟、SQI 时序、网卡抖动或长时间运行可靠性。
本次没有下载、复位或使能硬件。

构建日志：`tmp/irq_recovery_fix_20261015_build.log`。

固件：`program/FCM4E1553 Ethercat/MDK-ARM/N32G455/N32G455.axf`。

固件 SHA-256：
`5d11db80b87d8ac7ad22138b87ff4125db1733f549759b67f33118615bd330bf`。

构建前后 `Project.uvoptx` SHA-256 均为
`dec98658edf7aa652f4aa106d4e9dfe80a3e948279c3b43ab26ed697c6c0d3fd`，
本次没有改动用户的断点和 Watch 配置。

## 下载后的观察

下载该 AXF 后，先核对 `gEscIrqDiag.version = 0x20261015`，再按原步骤
人工恢复、使能和运行。PLC 使用现有 NewProject 1.1 即可。

关注 `gEscIrqDiag` 的变化，同时保留 `gEcatSyncDiag`、
`gEcatCommDiag`、`gEscPdiDiag` 和 PLC 的首错快照。如果再次停机，先保存
这些数据再复位，以区分补触发、同步故障、SQI 故障和主站 WcState 停机。

这次修正针对从站中断入口及失去边沿后的补救。主站发帧抖动仍需独立验证，
不能用“补触发计数增加”或短时间不停机认定主站通信问题已解决。
