# MCU 通信异常修正与实板验证

日期：2026-10-12。适用工程：`program/FCM4E1553 Ethercat/MDK-ARM/Project.uvprojx`，目标 `N32G455`。

## 1. 这次解决什么，没有证明什么

你提供的 PLC 首错记录是 `nStopReason=1`、`diagWcState=TRUE`，随后 `bStopLatched=TRUE`、控制字变为 0。直接停机原因是 PLC 检测到工作计数器异常并锁存停机，不是运行圈数达到上限。

WcState 是主站对 EtherCAT 命令的判断，不是 MCU 的 SQI 超时标志；也不能把累计的 `nBadWcCycles=3` 直接等同于三次独立丢帧。参见 [Beckhoff WcState/FrmXWcState 说明](https://infosys.beckhoff.com/content/1033/tcsystemmanager/1089009035.html)。

本次源代码检查确认了三个缺陷，并进行了修正：

1. SQI 忙等待超时后原来仍继续读写，且调用方不知道失败。
2. 原来只等待 Busy 清零，没有确认接收 FIFO 是否包含完整数据；PDO 映射无条件执行。
3. SyncManager 使能/禁用应答原来无限等待，通信异常时可能卡住主循环。

**这些是已确认的代码问题，但没有实板证据证明它们就是这次偶发 WcState 异常的根因。不能把本版称为“自动停转问题已全部解决”。**

没有修改 PLC、ESI/XML、PDO 布局、2 ms 周期、SQI 分频或现有 DC 同步保护；没有取消 WcState 停机保护。没有自动烧录或启用电机。用户已有的 `SaveFunc.c` 修改保留。

## 2. 固件行为变化

### 2.1 SQI/PDI 访问

- `qspi_read/qspi_write` 返回成功或失败；单次底层访问限定为 1～4 字节，上层分块。
- 任一等待超时立即记录首错并中止此次 MCU QSPI 事务，不继续取 FIFO 数据，不自动重放半次写入，不复位 ESC。
- Busy 清零后还检查 RX FIFO 数据数量；短读也按失败处理。
- DWT 可用时，每次忙等待预算为 100 μs；没有 DWT 时保留有限循环上限。100 μs 是软件等待上限，不是声明整个临界区小于 100 μs，也不是正常通信周期。
- `HW_EscRead*` 暂存完整逻辑块，成功才拷贝；发生传输失败时返回失败并清零请求的有效目标缓冲区，避免旧事件位或栈垃圾被解释为新数据。
- PDO 读取失败不执行 `APPL_OutputMapping`；写入失败停止发送后续块并记录。已经写到 ESC 的前半部分不能回滚。
- 非对齐读取按 4 字节边界拼接；当前读块上限为 256 字节，与本工程邮箱/PDO 最大值一致。以后扩大这些配置时必须同步修改并重新验证。
- SyncManager 应答最多检查 256 次；失败返回并锁存 PDI 故障，不再无限等待。次数不是毫秒数。

底层硬件接口现在有返回值，但 SSC 中仍有只使用兼容接口的通用调用点。这不是完整协议栈事务/邮箱恢复机制的认证；本次重点验证的是底层访问失败、PDO 提交和运动禁止路径。

### 2.2 检出 MCU PDI 故障时

`gEscPdiDiag.faultLatched` 置 1 后：

1. 下一次电机中断跳过运动算法，给四路 PWM 写入 0；相位指令路径也禁止继续推进。
2. 主循环向 CiA402 报告本工程已有的通信错误码 `0x7500`，`0x1001` 设置通用错误和通信错误位。
3. 通信自行恢复不解除该锁存，不自动恢复旧运动。

这会撤去电机驱动输出，不是受控减速，也不是 STO。断电后可能因惯性、负载或重力继续运动；必须使用合适的机械支撑/制动，并先在无危险负载的台架上试验。

### 2.3 手动解除 MCU PDI 故障的条件

全部满足才接受故障复位：

- 从站为正常 OP，输出更新已启用。
- 最近一次 PDI 错误后，至少收到 32 次完整输出 PDO，并有至少 32 次成功底层访问；2 ms 周期下 32 次 PDO 约 64 ms，但程序按有效次数判定，不按墙钟时间保证。
- 错误发生后，收到过低位控制请求及 Fault Reset 位均为 0 的新 PDO；即先发送控制字 0。
- 随后通过完整 PDO 收到禁用状态下的故障复位上升沿，典型为控制字 `0x0080`，不是 `0x008F`。恢复条件尚未满足时过早发出的复位，不会因一直保持高位而在稍后自动生效；需重新发送 0，再发送 0x0080。仅用 SDO 改该对象也不能解除本次 PDI 锁存。
- 软件实际速度为 0，原有硬件故障复位条件也满足。

复位只清除运动禁止，不清首错证据；还需要重新走使能过程。软件速度为 0 不等于机械已经安全静止，应现场确认。

## 3. Keil 中查看哪些变量

打开 Watch，添加下面的**整个全局变量名称**，再展开结构体；不要只输入结构体成员的裸名字。

```text
gEscPdiDiag
gEcatCommDiag
gEcatSyncDiag
gEscQspiMaxCriticalCycles
gMotorIsrMaxCycles
gMotorIsrPeriodCycles
gMotorIsrOverrunCount
gEcatMotorEnabled
```

保持 MCU 正常运行，不在运动中打断点、暂停或单步。否则调试暂停本身会影响实时通信，污染停机证据。若显示 `<cannot evaluate>`，先确认烧录的 AXF 与当前构建一致、全局结构体名称正确、使用支持运行中读取的调试方式；不要为读值在运动中停 CPU。

### 3.1 `gEscPdiDiag`：底层访问诊断

| 字段 | 用途 |
|---|---|
| `faultLatched` | 当前是否因 MCU PDI 故障禁止运动 |
| `firstError` | 上电后首个错误类型，最后写入，非 0 表示首错记录已提交 |
| `firstAddress / firstLength` | 首错底层访问地址和长度，通常是分块后的 4 字节访问 |
| `firstStatus / firstRxCount` | 中止前 QSPI 状态和 FIFO 数量 |
| `firstException` | Cortex-M 异常号；0 是线程/主循环，不是错误号 |
| `firstCycle` | DWT 周期计数快照，不是上电秒数 |
| `lastError` | 最近一个底层错误类型 |
| `timeoutCount` | 忙等待超时累计次数；不包括所有短读/参数错误 |
| `readFailureCount / writeFailureCount` | 累计失败次数，达到最大值后饱和 |
| `errorEpoch` | 错误代次，用于防止旧的成功 PDO 满足复位条件 |
| `transferCount / successSinceError` | 成功的底层分块访问总数/最近错误后的成功数，不是 EtherCAT 帧数 |

`firstError/lastError` 含义：

| 值 | 含义 | 重点检查 |
|---|---|---|
| 0 | 没有记录到相应错误 | 不等于主站没有 WcState 异常 |
| 1 | 无效指针、长度或地址范围 | 调用参数、PDO/邮箱配置 |
| 2 | 新事务开始前仍忙，等待超时 | 前次事务、QSPI 外设状态 |
| 3 | 读事务完成等待超时 | SQI 时序、外设忙状态 |
| 4 | Busy 已清零但 RX FIFO 数据不足 | FIFO 数量、SQI 事务是否完整 |
| 5 | 写事务完成等待超时 | SQI 写入及外设状态 |
| 6 | SyncManager 应答超出有限等待次数 | SM 配置、PDI 写入及读回应答 |

首错信息保留到 MCU 重启；手动复位成功后 `firstError` 仍可能非 0，应结合 `faultLatched` 和计数增量判断是否又发生故障。

### 3.2 `gEcatCommDiag`：PDO 与状态变化

| 字段 | 用途 |
|---|---|
| `pdoReadOkCount` | 完整读入并映射的输出 PDO 次数 |
| `pdoReadErrorCount / pdoWriteErrorCount` | 输出读取/反馈写入失败次数 |
| `lastPdoCycle / maxPdoGapCycles` | DWT 下的 PDO 处理时刻/最大间隔；包括主动停止通信的间隔 |
| `firstOpExitValid` | 是否保存过首个离开 OP 或进入 OP 错误状态的记录 |
| `firstOpExitFrom / To / Code` | MCU 请求的状态变化及 AL 状态码，不是对 ESC 写入成功的证明 |
| `lastAlState / lastAlCode` | 最近的软件 AL 状态记录 |
| `driveDisableCount` | 运行使能被状态门控撤销的次数，不覆盖所有硬件故障路径 |
| `firstDisableValid` | 是否保存过首个上述使能撤销记录 |
| `firstDisableControlWord / StatusWord / AlState / Position / PdiError` | 撤销使能时软件看到的控制字、状态字、AL 状态、位置及最近 PDI 错误 |
| `pdoSincePdiError / pdoErrorEpoch / resetLowSeen` | 手动恢复的条件记录 |
| `resetRequestPending` | 已从有效 PDO 接收到新的复位请求；接受后自动清零 |
| `manualResetCount` | 成功解除 MCU PDI 运动禁止的次数 |

`gEcatSyncDiag` 保留原有同步首错诊断。三个结构体是不同代码时刻的快照，不是一个同时采样的总线抓包。

DWT 的 32 位计数会回绕；不要把跨很长时间的差值当成精确停机时长。字段为 0 也可能是尚未启用周期计数器。换算 μs：`cycles * 1000000 / SystemCoreClock`。不要直接认定 CPU 一定是某个频率。

## 4. 从编译到首次运行

1. 让现有 PLC 的 `bRun=FALSE`、`bEnable=FALSE`，确认电机和机械机构安全静止。保存旧固件与工程备份。
2. 在 Keil 打开本工程 `Project.uvprojx`，选择 `N32G455`，执行 Rebuild。确认没有错误。此次没有新增源文件到 Keil 工程，无需手动添加编译单元。
3. 由你手动下载新固件，随后正常运行 MCU。下载时不要让 PLC 保持使能请求。本次助手没有执行下载。
4. TwinCAT 保持原来的 `GVL_CSP_SMOOTH`、2 ms 周期及已经验证过的映射；不需要换 XML 或重新建 PLC。
5. 先不使能，确认 OP、实时 `iWcState=FALSE`；记录三个 MCU 结构体和主站 Lost Frames、Tx/Rx Errors 的初始值。
6. 低速、低负载试运行：先 `bEnable=TRUE`，等待 `bReady=TRUE`，再 `bRun=TRUE`。使用“写入值”，不使用 Force。
7. 测试中不要手动切换 Config/Run、重启从站、清通信计数或暂停 MCU。先短时间验证，再逐步延长到超过此前停机时长的测试；一次成功不能代表商用可靠性。

若上电就出现 PDI 错误，不要强制清零结构体或禁止保护；保存 `firstError/firstAddress/firstStatus/firstRxCount`，先处理初始化或硬件通信问题。

## 5. 再停一次时怎么判断

不要先重启或复位。先确认机械安全，再保存：

- PLC：`nStopReason`、`diagWcState`、`diagEthercatState`、`diagStatusWord`、`diagControlWord`、`diagCycle`，以及实时值。
- MCU：完整 `gEscPdiDiag`、`gEcatCommDiag`、`gEcatSyncDiag`。
- 主站：Lost Frames、Tx/Rx Errors、CRC，相对启动前的增量，以及停止时的错误日志。

按下面的证据分支继续，不凭单个数字认定根因：

| 现象 | 可以支持的结论 | 不能直接支持的结论 |
|---|---|---|
| `firstError` 新增非 0，`faultLatched=1` | MCU 捕获了 PDI 访问失败，查看错误类型/地址 | 不能仅此断言是供电还是驱动时序问题 |
| `firstOpExitValid=1`，同步首错也有记录 | MCU 记录到了状态变化/同步失败路径 | 软件记录不证明失败原因一定是 DC 配置 |
| PLC 首错为 WcState，MCU PDI 未报错，AL 没记录离开 OP，随后看到控制字 0 | 支持“PLC 通信保护发出停机请求”这一链路 | 不能证明网卡损坏，也不能排除未被 MCU 检出的 ESC/链路异常 |
| 运行中计数停止增长或结构体全部重新初始化 | 需要进一步区分 CPU 停止、复位、调试暂停或读取问题 | 本版没有持久化复位原因记录，不能单凭计数变 0 断言 MCU 自发重启 |

正常人工禁用、重新激活配置也可能占用“首个退出 OP/首个撤销使能”记录。若要观察一次新的故障，先保存旧证据，再在安全条件下从重新上电的干净测试开始；不要在旧记录未保存时重启。

## 6. 如何恢复测试

### 仅 PLC 锁存，MCU 没有 PDI 故障

保存首错后，确认通信正常、电机静止、`bEnable/bRun=FALSE`、`bFault=FALSE`，写一次 `bResetDiag=TRUE`。确认 `bStopLatched=FALSE`，再按使能、Ready、Run 的顺序启动。

### MCU 也锁存 PDI 故障

1. `bEnable/bRun` 保持 FALSE，确保主站继续发送控制字 0，通信恢复并稳定。
2. 等待 `pdoSincePdiError>=32`、`successSinceError>=32`、`resetLowSeen=1`，确认电机实际静止。
3. PLC 看到 `bFault=TRUE` 后，写一次 `bResetFault=TRUE`，不要强制保持。
4. 观察 `faultLatched` 清零、CiA402 Fault 消失；如未清除，检查恢复条件，不要反复自动复位。
5. 然后按上一节清除 PLC 的 `bStopLatched`，重新使能。

若中止事务后实际硬件无法恢复通信，保留记录后停机检查，必要时在安全条件下重新上电。本版没有盲目复位 ESC 或自动重启电机的逻辑。

## 7. 验证边界

自动化工具：

```text
tests/verify_pdi_faults.py          实际构建 ARM 固件的异常路径测试，硬件接口模拟
tests/verify_ethercat_sync.js      源代码耦合的同步事件顺序回放
tests/verify_pulse_position.py    实际 ARM 固件定位/相位/PLC 桥接回归
```

测试不连接 MCU、网线或电机，不验证真实引脚时序、SQI 电气质量、网络 WKC、掉电行为、机械精度或长时间无故障运行。通过这些测试只是允许进入受控实板验证，不是商用或安全认证结论。

### 本次离线验证记录（2026-10-12）

| 项目 | 结果及边界 |
| --- | --- |
| Keil N32G455 构建 | 成功，0 错误；全量构建有 10 项既有警告，最终增量构建 0 警告；未由助手下载到硬件 |
| ARM 通信故障注入 | 17 项通过，执行当前 AXF 中的固件代码，硬件接口模拟 |
| 同步事件回放 | 22 项通过，源码耦合的离线回放，不等同于实板时序验证 |
| 脉冲／位置与 PLC 桥接回归 | 通过，含正反向端点、CSP 平滑及暂停、CSV 和故障关断；未验证真实转子位置 |
| 补丁格式检查 | `git diff --check` 通过 |

用户反馈烧录 SQI 修正版后仍停机：本次截图中 `nStopReason=1`、`diagWcState=TRUE`、`diagPhase=1`、`nBadWcCycles=2`，而停机后实时状态已恢复为 OP／WcState=0。该记录表明 PLC 因无效过程数据锁存停止；累计无效周期数不能直接解释为连续丢帧次数。尚未取得同次停机的 MCU 结构体记录，因此不能判定是否触发本次 PDI 故障检测，也不能宣称随机停转已修复。
