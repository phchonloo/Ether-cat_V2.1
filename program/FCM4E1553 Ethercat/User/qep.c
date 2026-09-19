#include "qep.h"
#include "PulseHandle.h"

#define QEP_PHASE_PINS (GPIO_PIN_0 | GPIO_PIN_1)
#define QEP_INDEX_PIN  GPIO_PIN_2
#define QEP_CLOCKS     (RCC_APB2_PERIPH_GPIOA \
                        | RCC_APB2_PERIPH_AFIO)

/*
 * QEP硬件连接：
 *
 * X1/PA0 -> TIM2_CH1 -> 编码器A相。
 * X2/PA1 -> TIM2_CH2 -> 编码器B相。
 * X3/PA2 -> TIM2_CH3 -> 编码器Z相。
 *
 * TIM2使用硬件编码器模式对A、B两相进行四倍频计数。Z相使用TIM2_CH3
 * 下降沿输入捕获，不占用EXTI2；EXTI2继续留给EtherCAT SYNC1。
 * qep_update()在ADC完成中断中每62.5 us调用一次，把TIM2的16位计数差
 * 累加为32位位置值。
 */

static volatile int32_t s_position;
static volatile int32_t s_index_position;
static volatile int16_t s_delta;
static volatile uint16_t s_previous_counter;
static volatile uint8_t s_index_valid;

/**
 * @brief 初始化TIM2硬件正交编码器接口。
 *
 * PA0和PA1使用TIM2编码器模式3，A、B两相边沿都参与计数。
 * PA2使用TIM2_CH3捕获Z相下降沿，并保存出现Z相时的位置。
 * 三路信号来自板级光耦，GPIO使用内部上拉。
 */
void qep_init(void)
{
    GPIO_InitType gpio_init;
    TIM_TimeBaseInitType timer_init;
    TIM_ICInitType capture_init;
    NVIC_InitType nvic_init;

    RCC_EnableAPB2PeriphClk(QEP_CLOCKS, ENABLE);
    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM2, ENABLE);

    gpio_init.Pin = QEP_PHASE_PINS | QEP_INDEX_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpio_init);

    TIM_DeInit(TIM2);
    TIM_InitTimBaseStruct(&timer_init);
    timer_init.Prescaler = 0U;
    timer_init.Period = 0xFFFFU;
    timer_init.ClkDiv = TIM_CLK_DIV1;
    timer_init.CntMode = TIM_CNT_MODE_UP;
    timer_init.RepetCnt = 0U;
    TIM_InitTimeBase(TIM2, &timer_init);

    /* A相：PA0/TIM2_CH1，每个有效边沿都参与编码器计数。 */
    TIM_InitIcStruct(&capture_init);
    capture_init.Channel = TIM_CH_1;
    capture_init.IcPolarity = TIM_IC_POLARITY_RISING;
    capture_init.IcSelection = TIM_IC_SELECTION_DIRECTTI;
    capture_init.IcPrescaler = TIM_IC_PSC_DIV1;
    capture_init.IcFilter = 8U;
    TIM_ICInit(TIM2, &capture_init);

    /* B相：PA1/TIM2_CH2，滤波参数与A相保持一致。 */
    capture_init.Channel = TIM_CH_2;
    TIM_ICInit(TIM2, &capture_init);

    TIM_ConfigEncoderInterface(TIM2,
                               TIM_ENCODE_MODE_TI12,
                               TIM_IC_POLARITY_RISING,
                               TIM_IC_POLARITY_RISING);

    /* Z相：PA2/TIM2_CH3，光耦输出低有效，所以捕获下降沿。 */
    capture_init.Channel = TIM_CH_3;
    capture_init.IcPolarity = TIM_IC_POLARITY_FALLING;
    TIM_ICInit(TIM2, &capture_init);

    TIM_SetCnt(TIM2, 0U);
    TIM_ClearFlag(TIM2, TIM_FLAG_CC3 | TIM_FLAG_CC3OF);
    TIM_ConfigInt(TIM2, TIM_INT_CC3, ENABLE);

    nvic_init.NVIC_IRQChannel = TIM2_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 2U;
    nvic_init.NVIC_IRQChannelSubPriority = 3U;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);

    s_position = 0;
    s_index_position = 0;
    s_delta = 0;
    s_previous_counter = 0U;
    s_index_valid = 0U;

    TIM_Enable(TIM2, ENABLE);
}

/**
 * @brief 将TIM2本周期计数变化累加到32位位置。
 * @note 由ADC完成中断每个16 kHz控制周期调用一次。
 */
void qep_update(void)
{
    uint16_t current_counter;

    current_counter = TIM_GetCnt(TIM2);
    s_delta = (int16_t)(current_counter - s_previous_counter);
    s_position += s_delta;
    s_previous_counter = current_counter;
}

/**
 * @brief 设置当前位置并清零TIM2硬件计数器。
 * @param[in] position 新的32位位置值。
 * @note 只在电机停止或已经确认Z相位置时调用。
 */
void qep_reset(int32_t position)
{
    TIM_Enable(TIM2, DISABLE);
    TIM_SetCnt(TIM2, 0U);

    s_position = position;
    s_delta = 0;
    s_previous_counter = 0U;
    s_index_valid = 0U;

    TIM_ClearFlag(TIM2, TIM_FLAG_CC3 | TIM_FLAG_CC3OF);
    TIM_Enable(TIM2, ENABLE);
}

/**
 * @brief 读取累计的32位编码器位置。
 */
int32_t qep_get_position(void)
{
    return s_position;
}

/**
 * @brief 读取最近一个16 kHz控制周期的编码器计数变化量。
 */
int16_t qep_get_delta(void)
{
    return s_delta;
}

/**
 * @brief 读取一次已经捕获的Z相信号位置。
 * @param[out] position Z相下降沿出现时的32位位置。
 * @return 1表示返回了新的Z相位置，0表示没有新的Z相信号。
 */
uint8_t qep_get_index(int32_t *position)
{
    if ((position == 0) || (s_index_valid == 0U))
    {
        return 0U;
    }

    NVIC_DisableIRQ(TIM2_IRQn);
    *position = s_index_position;
    s_index_valid = 0U;
    NVIC_EnableIRQ(TIM2_IRQn);

    return 1U;
}

/**
 * @brief TIM2的Z相输入捕获中断。
 *
 * CCDAT3保存Z相下降沿出现时的TIM2计数值。用该值相对最近一次
 * qep_update()计数值的差，得到更准确的Z相32位位置。
 */
void TIM2_IRQHandler(void)
{
    uint16_t index_counter;

    IoCaptureHandle();

    if (TIM_GetIntStatus(TIM2, TIM_INT_CC3) != RESET)
    {
        index_counter = TIM_GetCap3(TIM2);
        s_index_position = s_position
                           + (int16_t)(index_counter
                                       - s_previous_counter);
        s_index_valid = 1U;
        TIM_ClrIntPendingBit(TIM2, TIM_INT_CC3);
    }

    if (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC3OF) != RESET)
    {
        TIM_ClearFlag(TIM2, TIM_FLAG_CC3OF);
    }

    if (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC1OF) != RESET)
    {
        TIM_ClearFlag(TIM2, TIM_FLAG_CC1OF);
    }

    if (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC2OF) != RESET)
    {
        TIM_ClearFlag(TIM2, TIM_FLAG_CC2OF);
    }
}
