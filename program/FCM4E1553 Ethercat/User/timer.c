#include "mcuinit.h"

/*
 * 定时器资源说明：
 *
 * TIM2：仅在脉冲/方向中断接口显式启用时使用PA0、PA1；PA2保持空闲。
 * TIM6：EtherCAT协议栈本地时基，与PWM/ADC同步无关。
 * TIM8：四路PWM计数器和ADC3采样周期主定时器。
 *
 * PWM/ADC硬件触发链：
 * TIM8更新TRGO -> ADC3常规序列 -> DMA2_CH1 -> adc_value[]。
 */

/**
 * @brief 初始化EtherCAT协议栈使用的TIM6本地时基。
 * @param[in] period 原协议栈使用的定时周期倍数。
 */
void timer_init(uint8_t period)
{
    TIM_TimeBaseInitType timer_init;

#if ECAT_TIMER_INT
    NVIC_InitType nvic_init;
#endif

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM6,
                            ENABLE);

    TIM_DeInit(TIM6);
    timer_init.Period = (uint16_t)(period * 200U);
    timer_init.Prescaler = 41U;
    timer_init.ClkDiv = TIM_CLK_DIV1;
    timer_init.CntMode = TIM_CNT_MODE_UP;
    timer_init.RepetCnt = 0U;
    timer_init.CapCh1FromCompEn = false;
    timer_init.CapCh2FromCompEn = false;
    timer_init.CapCh3FromCompEn = false;
    timer_init.CapCh4FromCompEn = false;
    timer_init.CapEtrClrFromCompEn = false;
    timer_init.CapEtrSelFromTscEn = false;
    TIM_InitTimeBase(TIM6, &timer_init);
    TIM_ClearFlag(TIM6, TIM_FLAG_UPDATE);

#if ECAT_TIMER_INT
    TIM_ConfigInt(TIM6, TIM_INT_UPDATE, ENABLE);
#endif

    TIM_Enable(TIM6, ENABLE);

#if ECAT_TIMER_INT
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    nvic_init.NVIC_IRQChannel = TIM6_IRQn;
    /* EtherCAT时基与ESC/SYNC中断同为次高抢占级，内部排序最后。 */
    nvic_init.NVIC_IRQChannelPreemptionPriority = 1U;
    nvic_init.NVIC_IRQChannelSubPriority = 3U;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
#endif
}

/**
 * @brief 初始化PWM和ADC共用的TIM8时基。
 *
 * 触发源：TIM8更新事件输出的TRGO硬件信号。
 * ADC触发模式：TIM8更新TRGO直接启动ADC3常规序列。
 * 触发频率：16 kHz，即每62.5 us触发一组四路采样。
 *
 * TIM8使用系统时钟、2分频和中心对齐模式1。计数器的完整周期为：
 *
 *     0 -> ARR -> 0
 *
 * 因此PWM频率计算式为：
 *
 *     PWM频率 = 系统时钟 / ((PSC + 1) * 2 * (ARR + 1))
 *
 * 当前系统时钟为144 MHz时，PSC为1、ARR为2249，完整往返周期
 * 为62.5 us，PWM频率为16 kHz。CCR范围为0～2250。
 *
 * 中心对齐模式在ARR顶部和0底部都会提出一次更新请求。RepetCnt设为1，
 * 让两个更新请求合成一次真正的更新事件，所以每个完整PWM周期只输出
 * 一次TRGO。触发由TIM8硬件完成，不受EtherCAT中断执行时间影响。
 */
void timer_pwm_adc_init(void)
{
    TIM_TimeBaseInitType timer_init;
    RCC_ClocksType clocks;
    NVIC_InitType nvic_init;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_TIM8, ENABLE);
    RCC_ConfigTim18Clk(RCC_TIM18CLK_SRC_SYSCLK);
    RCC_GetClocksFreqValue(&clocks);

    TIM_DeInit(TIM8);
    TIM_InitTimBaseStruct(&timer_init);
    timer_init.Prescaler = 1U;
    timer_init.CntMode = TIM_CNT_MODE_CENTER_ALIGN1;
    timer_init.Period =
        (uint16_t)(clocks.SysclkFreq
                   / ((timer_init.Prescaler + 1U) * 2U * 16000U)
                   - 1U);
    timer_init.ClkDiv = TIM_CLK_DIV1;
    timer_init.RepetCnt = 1U;
    TIM_InitTimeBase(TIM8, &timer_init);

    TIM_ConfigArPreload(TIM8, ENABLE);
    TIM_SetCnt(TIM8, 0U);

    /*
     * 装载PSC、ARR和重复计数值时暂时不选择UPDATE作为TRGO，避免初始化
     * 产生的软件更新事件提前启动一次ADC转换。
     */
    TIM_SelectOutputTrig(TIM8, TIM_TRGO_SRC_ENABLE);
    TIM_GenerateEvent(TIM8, TIM_EVT_SRC_UPDATE);
    TIM_ClearFlag(TIM8, TIM_FLAG_UPDATE);

    /* 每个完整PWM周期的更新事件直接输出一次硬件TRGO。 */
    TIM_SelectOutputTrig(TIM8, TIM_TRGO_SRC_UPDATE);
    TIM_SelectMasterSlaveMode(TIM8,
                              TIM_MASTER_SLAVE_MODE_ENABLE);

    /* N32G455的TIM8更新事件使用独立的TIM8_UP中断向量。 */
    TIM_ClearFlag(TIM8, TIM_FLAG_UPDATE);
    TIM_ConfigInt(TIM8, TIM_INT_UPDATE, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    nvic_init.NVIC_IRQChannel = TIM8_UP_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 0U;
    nvic_init.NVIC_IRQChannelSubPriority = 1U;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
}

/**
 * @brief 启动PWM和ADC共用的TIM8。
 *
 * pwm_config()完成四路输出比较配置后调用本函数。TIM8开始计数后：
 * 1. PC6～PC9开始产生中心对齐PWM；
 * 2. 每个完整PWM周期产生一次TRGO；
 * 3. TIM8更新TRGO直接启动ADC3常规序列；
 * 4. DMA完成一轮10个结果后进入DMA2_CH1中断。
 */
void timer_pwm_adc_start(void)
{
    /* 从0开始，保证PWM周期和ADC触发相位在每次上电时一致。 */
    TIM_SetCnt(TIM8, 0U);
    TIM_ClearFlag(TIM8, TIM_FLAG_UPDATE);
    TIM_Enable(TIM8, ENABLE);
}
