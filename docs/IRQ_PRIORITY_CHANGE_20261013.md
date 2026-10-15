# IRQ / SYNC0 优先级与未接线 SYNC1 屏蔽（2026-10-13）

## 修改范围

按用户最新要求，保留 EXTI0 / PB0 的 ESC IRQ 为唯一的抢占级 0；提升 EXTI1 / PB1 的 SYNC0，使其能够抢占电机控制；屏蔽未接线的 PB2 / EXTI2 外部 SYNC1 中断。
这里只改变 CPU 中断调度和板级 SYNC1 中断开关，不改变 PDO 映射、运动速度、PWM/ADC 硬件频率、故障停机或人工复位条件。

NVIC 保持 `NVIC_PriorityGroup_2`：2 位抢占优先级、2 位子优先级。数值越小越优先。

| 中断 | 抢占级 | 子优先级 | 变更 |
| --- | ---: | ---: | --- |
| EXTI0：ESC IRQ / PB0 | 0 | 0 | 保持最高 |
| EXTI1：SYNC0 / PB1 | 1 | 0 | 高于电机，能够抢占 TIM8 |
| TIM8_UP：16 kHz 电机控制 | 2 | 0 | 两处配置同时由 1 改为 2 |
| EXTI2：SYNC1 / PB2 | — | — | EXTI 和 NVIC 均屏蔽，不配置优先级 |
| TIM6：协议栈时基 | 2 | 0 | 保持不变 |
| DMA2_Channel1：ADC 完成 | 2 | 1 | 保持不变 |
| USART1 / UART5 | 3 | 0 | 保持不变 |
| DMA1_Channel4 / Channel6：串口 TX | 3 | 1 | 保持不变 |

备用 CAN 接收仍为 `2,1`，备用 TIM2 捕获仍为 `2,3`，本次不启用备用接口。

## 生效位置

- `program/FCM4E1553 Ethercat/User/exti.c`：`exti_init_irq()` 配置 EXTI0 为 `0,0`。
- `program/FCM4E1553 Ethercat/User/exti.h`：`ECAT_SYNC1_IRQ_ENABLED=0U`，明确当前硬件没有 SYNC1 连线。
- `program/FCM4E1553 Ethercat/User/exti.c`：SYNC0 为 `1,0`；未接线的 SYNC1 不配置 PB2，屏蔽 EXTI2 中断和事件、关闭边沿触发、清除挂起，并禁用 EXTI2 NVIC 中断。
- `program/FCM4E1553 Ethercat/Ethercat/port/el9800hw.c`：禁止启动代码重新使能 SYNC1；即使意外进入 EXTI2 向量，也只禁用和应答，不分派外部 SYNC1 处理。
- `program/FCM4E1553 Ethercat/User/timer.c`：`timer_pwm_adc_init()` 配置 TIM8_UP 为 `2,0`。
- `program/FCM4E1553 Ethercat/User/main.c`：启动后的 TIM8_UP 再配置也改为 `2,0`，避免覆盖。
- 更新电机命令双缓冲和耗时统计的注释，使其与新的抢占关系一致；未改变这些逻辑。

主站继续保持只启用 SYNC0、禁用 SYNC1。当前 2 ms 配置对应 SYNC0 周期 2000 us；本次不修改该周期。
板级屏蔽不等于关闭 `DC_SUPPORTED`：SYNC0/DC 检测继续有效，协议栈对 ESC 内部同步事件的处理保留，不将物理引脚屏蔽误用为同步保护旁路。

## “不被打断”的准确含义

ESC IRQ 服务运行时，其余已配置普通中断不能抢占它；它可以抢占未处于屏蔽区的电机控制中断。
这不代表 IRQ 信号到达后一定立即执行，也不保证硬实时期限：

1. NMI 和 HardFault 仍有更高的固定优先级。
2. SQI 事务的 `PRIMASK` 全局关中断保护保留，它会延迟 IRQ 的进入。
3. SYNC0、TIM6 处理中的 `DISABLE_ESC_INT()` 保护保留，它会暂时屏蔽 EXTI0。
4. IRQ 不会重入抢占自己；挂起与重复事件仍取决于 EXTI/ESC 事件处理机制。
5. SYNC0 的抢占级 1 高于电机的 2，能够抢占未处于屏蔽区的 TIM8；不是仅改变同级等待顺序。
6. TIM8、TIM6、ADC DMA 同为抢占级 2，不能互相抢占；同时挂起时按子优先级仲裁，同子优先级再按 IRQ 编号仲裁。

不移除上述保护，是为了避免改变协议栈与 SQI 的并发约束。若要进一步压缩屏蔽时间，需单独测量并审查，不能只删除关中断语句。

## 风险与实机验证

IRQ 和 SYNC0 优先于电机后，可能增加电机控制 ISR 的响应延迟或执行跨度。PWM/ADC 的硬件周期仍是 16 kHz，但 CPU 控制计算的实时性需要重新验证。
此调整仅用于验证通信优先调度，不代表已经确定或消除 `0x001A` 同步错误和 WcState 异常的根因。

由用户在机械安全、负载受控、PLC 不使能的条件下编译和下载。先无运动观察，再决定是否低速测试；助手不下载固件、不复位、不使能或启动电机。
通信恢复后仍必须经过原有人工复位和使能流程，不允许自动恢复运动。

持续运行调试时可观察：

- `gMotorIsrMaxCycles`：TIM8 从入口到出口的最大跨度，包含被 IRQ 和 SYNC0 抢占的时间。
- `gMotorIsrPeriodCycles`：16 kHz 对应的周期预算；144 MHz 时为 9000 个周期。
- `gMotorIsrOverrunCount`：入口到出口跨度达到周期预算的次数；它不覆盖所有入口等待时间，不能单独证明没有漏周期。
- `gMotorIsrLoadPercent`：包含嵌套 IRQ / SYNC0 的耗时占比，不再能理解为纯电机算法 CPU 占用率。
- `gEscQspiMaxCriticalCycles`：SQI 单次全局关中断区间的最大耗时。

同时保存 IRQ、SYNC0 及（后续另行授权增加的）ISR 入口/出口标记波形。不要用断点、暂停或单步测量通信实时性。

## 离线验证

`tests/verify_pdi_faults.py` 包含三项板级配置回归：

1. 执行真实 ARM 初始化函数与 NVIC 写入，核对 `IRQ > SYNC0 > TIM8`，以及 SYNC1 不设置优先级。
2. 预置已开启的 EXTI2，再执行初始化和真实 `HW_Init()`，核对 SYNC1 被屏蔽、挂起清除请求，以及启动期间没有重新使能；同时确认 IRQ / SYNC0 仍使能。检查意外进入 EXTI2 向量时也不会分派 SYNC1。
3. 在模拟硬件的启动流程中执行 `main()`，确认启动后的第二处 TIM8 配置保持抢占级 2。

这些测试只验证编译后软件的配置与已有故障保护，不模拟真实 NVIC 抢占、线缆时序、控制环稳定性或电机负载。

构建目标：`program/FCM4E1553 Ethercat/MDK-ARM/Project.uvprojx` → `N32G455`。
固件产物：`program/FCM4E1553 Ethercat/MDK-ARM/N32G455/N32G455.axf`。

### 本次验证结果

| 项目 | 结果 |
| --- | --- |
| Keil N32G455 全量重新编译 | 0 错误、10 项原有警告；生成 AXF |
| ARM 故障保护与配置回归 | 25 项通过，包含优先级及 SYNC1 屏蔽检查 |
| 同步事件源码关联回放 | 22 项通过；不代表硬件时序通过 |
| ARM 脉冲位置与 PP/CSP/CSV 桥接回归 | 全部通过；包含新 PLC 三圈及 500 ms 间隔检查 |
| 用户 Project.uvoptx | 构建前后 SHA-256 一致，未修改原有调试设置 |
| 实机抢占、通信稳定性与运动验证 | 未执行；未下载、未复位、未使能电机 |

本次 AXF SHA-256：`3007bfee6d69c2e73bdb645f1c4bc70135b1721fd85a68e3c54a9693d7e9d7b9`。

## 回退参考

若实机出现电机控制超时或运行恶化，先安全停止。如需仅回退本次 SYNC0 提升，将 SYNC0 恢复为 `1,1`、TIM8 上述两处恢复为 `1,0`，仍保留 IRQ 的 `0,0` 和未接线 SYNC1 的屏蔽，然后重新编译下载。不要因回退优先级而启用未接线 SYNC1、撤销其他通信诊断修正或关闭停机保护。
