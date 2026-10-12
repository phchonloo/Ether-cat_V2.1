# 内部脉冲／相位计数定位：修改与验证

## 本次改变

板卡用四路 PWM 直接驱动绕组，不是向外输出 STEP/DIR。电机算法在
`N32G455.lib` 内，已核对其 `MotorFunc -> PhaseGet` 调用和相位标度。
本次没有修改该库，也没有直接启用旧的 `PulseSet()`：
那个函数本身仍在累加／递减速度量，并不是真正的外部脉冲计数器。

现在的位置依据是 **PhaseGet 实际推进的内部励磁相位**。
TIM8 中断开始前记录 Iangle，算法结束后记录相位差；没有推进就不增加位置。
根据目标脉冲数换算相位预算，在 ISR 限制最后一步，不能沿当前运动方向越过目标。
速度／加减速仍用于决定推进频率，位置终点由计数预算决定，不靠速度积分猜测。

适用 PP（1）、CSP（8）；PV/CSV 的反馈也改为实际相位计数。
PP 的 6 -> 7 -> 15 -> 47 -> 63 控制流程保留，CSP 的 PDO／PLC 映射不变。
PP Halt、OP 退出、驱动器故障和原有 PLC 通信保护均保留。
原有周期命令有效性检查也保留；本次不掩盖自动停机原因。

重要限制：这仍是开环励磁计数，**不是编码器测得的轴位置**。
机械丢步、联轴器打滑或负载变形不能由此检测；不要只凭 0x6064 判断机械精度。
内部相位单位的最后一点量化也受原有电流查表精度影响。

## 使用前

1. 打开原来的 `program/FCM4E1553 Ethercat/MDK-ARM/Project.uvprojx`，
   Rebuild 后手动下载。助手没有下载、启动或使能硬件。
2. 不需要换 XML、不需要重新扫描，也不需要改变 PLC PDO 映射。
   新的 EcatPulsePosition.h 被已有 C 文件包含，不需要额外加入源文件列表。
3. 初次在空载、低速、有独立急停的条件下验证，不连接危险负载。
4. 查看 `sxifen` 和 `gEcatPulseDiag.resolution`，应相等。
   默认 1000 逻辑脉冲／机械圈；PLC 的 nCountsPerRev 要与其相等。
   本工程已有分辨率表范围为 200～40000；运行中不要改 sxifen。
5. 运行时不要给 MCU 下断点，不要暂停 MCU，不要强制 Enable/Run。
   参数和启动请求使用 Write values。

## 先验证 CSP

2026-10-09 最新版本优先使用 examples/TwinCAT_CSP_SmoothRepeat：
按其中 README 新建独立 PLC，手动复制三个 ST 文件，不导入旧工程。
默认 2 秒单圈、停稳确认后暂停 0.5 秒；稳定后可试 1.5 秒。
旧 FastRepeat 文件保留作对照，不再作为最新推荐示例。
保持 PLC 周期和 SM/DC 目标周期为 2 ms；不要通过改变周期来改速度。

1. 在电机轴上做标记，记录当前 0x6064／iActualPosition。
2. 停稳并取消使能后清理旧的 PLC 诊断：
   bEnable=FALSE，bRun=FALSE，bResetDiag 写入一次 TRUE；
   确认 bDiagValid=FALSE、nStopReason=0。
3. 写入 bEnable=TRUE，等 bReady=TRUE，再写入 bRun=TRUE。
4. 每圈结束／暂停时，目标和计数位置应一致；核对机械轴标记是否一圈回到同一方向。
   程序若连续单方向转动，累计坐标每圈增加 sxifen，不应回到数值 0。
5. 先运行 20 圈，之后至少超过此前通常停转的时长，记录运行时间／圈数。
   没有停一次不等于随机停机已经根治。

## 再验证 PP 单次定位

PP 测试时，不要让正在运行的 CSP PLC 或 NC 轴同时写控制字、模式和目标位置。
使用已有的独立 PP 测试工程／方法；不要停止承担 EtherCAT 通信的循环任务。

1. 6060=1；6040 依次写 6、7、15，每一步等待对应状态反馈。
2. 6040=47（bit4 低），607A 写入“当前位置 + sxifen”，然后 6040=63 接受。
3. 等到目标后观察轴标记及计数位置。再以相同握手写回原来的起始位置。
4. 反复往返 20 次；不能只单向转，因为往返更容易发现方向、余数和累计误差。
5. 首次速度保守，例如 6081=30 rpm；本固件 6081/6083/6084 使用 rpm/rpm每秒。
   不要把斯达普说明书的参数单位直接套到自研固件。

## 新增调试变量

Keil Watch 展开 `gEcatPulseDiag`，全程不停 MCU 读取：

| 字段 | 含义与期望 |
|---|---|
| resolution | 本次初始化锁定的每圈逻辑脉冲数，与 sxifen 相同 |
| targetPosition | 本次交给脉冲执行器的目标 |
| countedPosition | 实际相位推进换算的逻辑位置；与 6064 同源 |
| atTarget | 位置模式下相位计数已到当前目标；不是编码器到位 |
| lastPhaseStep | 上一周期实际推进的相位量；停稳后为 0 |
| requestedPhaseStep | 上一周期准备推进的量 |
| mismatchCount | 请求量与实际 PhaseGet 推进量不一致的累计次数，正常运行应为 0 |
| positiveStepCycles | 正向相位推进的周期数 |
| negativeStepCycles | 负向推进周期数；重新上电后的纯正转测试应始终为 0 |
| blockedAwayCount | 目标反转时阻止旧方向斜坡继续远离目标的次数 |
| maxStepDelta | 最大相邻相位步长突变，除以 16 得对应 rpm 突变 |
| countedCycles | 调用了脉冲准备逻辑的周期数（包含保持目标的零增量周期） |
| skippedCycles | 未调用准备逻辑的周期数；启动／未准备好／故障时可能增加，不能单独当故障 |
| positionLimit | 计数或相位边界异常锁存；正常为 0 |
| resolutionChanged | 初始化值无效／运行中 sxifen 被改变；正常为 0 |

positionLimit／resolutionChanged 锁存后需要停机查因，修正分辨率并重新初始化；
不要强制这些标志为 0 来继续运行。
外部 6064/607A 仍是 DINT，连续单方向累计最终会到位置范围边界，并非“永远不停”。

如果再次自动停止：先截图 PLC 的 bDiagValid/nStopReason 和 diag* 首次记录，
再截图 gEcatPulseDiag、ErrNumFlag、gEcatMotorEnabled、gMotorIsrOverrunCount；
不要先复位，不要为了观察而暂停 MCU。
gMotorIsrOverrunCount 应不增长；新的计数接入仍需要在实机检查最坏中断耗时。

## 离线验证与范围

本次 Keil 完整重编译：0 错误，10 个原有警告（串口声明／未使用变量）。
计数核心通过 ARMCC 编译，由 Unicorn 执行真实 ARM 机器码：
17 种分辨率 × 6 个正反目标、125 万逻辑脉冲正向累计后返回、余数保持及 DINT 边界。
完整固件测试执行实际 PulseFunction、PhaseGet、PP/CSP 命令和计数接口，
包括未执行 PhaseGet 时不虚构位置、精确端点和原有保护。
测试不执行 GPIO/ADC/PWM 外设，不模拟机械负载或真实中断抖动。

最新 CSP 使用精确相位差的 20 ms 平滑跟随，去掉整数 rpm 前馈的历史方向，
保留加减速限制、细微计数累计和最后一步限幅。存在约 20 ms 跟随滞后。
新 PLC 五次轨迹保证起止速度/加速度为零，并要求状态字 bit10 到位后连续确认 100 ms。
新增 ARM 回归验证 1000/40000 分辨率、1.5/2 秒单圈正反轨迹无非预期反向/越点；
还通过新 PLC ST 源码与 ARM 固件的三圈联动。不是 TwinCAT 编译或机械仿真。

复现：先 Keil Rebuild，再运行 tests/verify_pulse_position.py；最新版还需要可执行的 Node.js。
脚本自动编译 tests/pulse_position_arm_test.c，需要 ARMCC5（默认 D:/App/Keil_v5/ARM/ARMCC/bin；
可用 KEIL_ARMCC_BIN 指定其他位置）与 Python unicorn==2.1.4、pyelftools==0.32。
依赖可在 tests/.pulse_test_deps 隔离安装，二进制输出在 tests/.pulse_test_build，均被忽略。

离线通过不能证明机械不丢步，也不能证明随机自动停机已解决；
最终以实机往返精度、长时间运行、故障记录和 ISR 最坏耗时为准。
