# DC 同步 PDI 竞态修正与验证

## 修改内容

修改 MCU 协议栈的 PDI 中断，不修改 PLC 示例、XML、中断优先级或电机算法。

- 入口没有 PDO、复读 AL Event 时首次发现 PDO：正常映射和更新 SM/SYNC0 计数，不再当成第二帧清掉。
- 已处理一次 PDO 后又发现输出事件：保留原 SSC 的超限计数及确认策略；16 位超限计数饱和，避免回绕为零。
- 保存各次 AL Event 快照的 SYNC0/SYNC1 位，每次 ISR 对每个同步事件最多服务一次。首次晚到 PDO 时最多读取三次 AL Event，没有循环排空邮箱或 PDO。
- OP 下同步故障仍按原条件请求 SAFEOP（0x001A / 0x002C）；没有关闭看门狗、提高容错阈值或自动恢复运行。

这些改动修正一个明确的软件竞态，但不保证所有 DC 同步故障都由它引起。

## 已做的离线检查

Keil ARMCC 5.06 update 6 整工程 Rebuild：0 errors，10 warnings，链接成功。
警告来自未修改的 SaveFunc.c、eeprom_console.c；修改的协议文件没有编译警告。
构建日志位于 program/FCM4E1553 Ethercat/MDK-ARM/N32G455/sync_fix_build.log。

在项目根目录执行：

~~~powershell
node tests\verify_ethercat_sync.js
~~~

22 项源码关联事件回放检查通过，包括首次晚到、真实重触发、同步位保留、SAFEOP 确认、SM 模式、计数饱和、原停机保护及首错锁存。

回放从当前 C 文件提取函数，仅机械转换简单语法后用 JavaScript 执行；ESC/SYNC 回调为模拟，不是 ARM 二进制测试，也不验证 SQI 字节序、硬件中断时延或电机实际运动。

## 实机操作

1. 先停转并禁用驱动，确认急停可用、无危险负载。不要带着运动命令下载固件。
2. 用 Keil 下载本次重新构建的 N32G455.axf，复位后全速运行。本次离线检查没有下载或操作 MCU。
3. 保持现有设置：PLC 周期 2 ms、DC SYNC0 2000 us。先不要同时修改 Shift、同步容错阈值或电机中断优先级。
4. Keil Watch 添加 gEcatSyncDiag，展开观察。测试期间不要打断点或暂停 MCU，也不要强制 PLC 使能变量。
5. 先保持 OP 停转，再做低速、空载 CSP 循环测试；分别记录开始和停止时的诊断值与 TwinCAT Error List。

| Watch 字段 | 含义 |
| --- | --- |
| lateOutputEventCount | 入口未看到、复读才看到且已正常服务的首次 PDO 次数 |
| outputRetriggerCount | 已服务首次 PDO 后，仍发现输出事件的次数 |
| firstAlStatusCode | 首次 OP 同步退出：0 未记录；0x001A 序列错误；0x002C DC 看门狗错误 |
| firstSmEventMissedCounter / firstSyncErrorLimit | 首错时的 SM 漏事件计数及阈值 |
| firstSmSync0Counter / firstSmSync0Limit | 首错时 SM/SYNC0 顺序计数及配置值 |
| firstCycleExceededCounter | 首错时的周期超限计数 |
| firstSync0WdCounter / firstSync0WdLimit | 首错时 SYNC0 看门狗计数及阈值 |
| firstDcRunning / firstSequenceValid | 首错时 DC 运行、同步序列有效标志 |
| firstEntryEvent / firstPendingEvent | 故障前最近一次 PDI 的入口和复读事件快照 |

首错记录跨 EtherCAT 状态切换保留，直到 MCU 复位。先保存记录再复位。
这些是可能被中断打断的软件快照，不是原子硬件时间戳；事件快照也不是故障时新读的 ESC 寄存器。

## 判断结果

首次晚到计数允许增长，但不能因此增加 1C32:0C；真正重触发仍可能增加该对象。
看输出事件重触发计数的增量，不能只看某一次读到的零值。

如果仍发生 OP -> SAFEOP，请保存 firstAlStatusCode 及上述首错字段，连同 TwinCAT 错误截图继续定位。不要屏蔽同步停机保护。
至少完成停转、运动、CoE 读取并行、长时间循环等实机测试，才能判断稳定性；当前未进行这些实机验证。
