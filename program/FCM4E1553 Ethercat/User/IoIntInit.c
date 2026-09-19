#include "IoIntInit.h"

#define IO_INPUT_PINS  (GPIO_PIN_0 | GPIO_PIN_1 \
                        | GPIO_PIN_2 | GPIO_PIN_3)
#define IO_OUTPUT_PC_PINS (GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12)
#define IO_OUTPUT_PA_PINS GPIO_PIN_15
#define MOTOR_ERROR_LED_PIN GPIO_PIN_8
#define MOTOR_VFO_PIN       GPIO_PIN_14
#define IO_GPIO_CLOCKS (RCC_APB2_PERIPH_GPIOA \
                        | RCC_APB2_PERIPH_GPIOB \
                        | RCC_APB2_PERIPH_GPIOC \
                        | RCC_APB2_PERIPH_AFIO)

static GPIO_Module *const s_input_ports[IO_INPUT_COUNT] = {
    GPIOA, GPIOA, GPIOA, GPIOA
};

static const uint16_t s_input_pins[IO_INPUT_COUNT] = {
    GPIO_PIN_0, GPIO_PIN_1, GPIO_PIN_2, GPIO_PIN_3
};

static GPIO_Module *const s_output_ports[IO_OUTPUT_COUNT] = {
    GPIOC, GPIOC, GPIOC, GPIOA
};

static const uint16_t s_output_pins[IO_OUTPUT_COUNT] = {
    GPIO_PIN_10, GPIO_PIN_11, GPIO_PIN_12, GPIO_PIN_15
};

static u8 s_legacy_capture_initialized;

static u8 io_read_raw(io_input_t input)
{
    uint32_t index;

    index = (uint32_t)input;
    if (index >= IO_INPUT_COUNT)
    {
        return 0U;
    }

    return (u8)GPIO_ReadInputDataBit(s_input_ports[index],
                                     s_input_pins[index]);
}

static void legacy_capture_timer_init(void)
{
    TIM_TimeBaseInitType timer_init;
    TIM_ICInitType capture_init;

    if (s_legacy_capture_initialized != 0U)
    {
        return;
    }

    RCC_EnableAPB1PeriphClk(RCC_APB1_PERIPH_TIM2, ENABLE);

    TIM_DeInit(TIM2);
    TIM_InitTimBaseStruct(&timer_init);
    timer_init.Prescaler = 0U;
    timer_init.Period = 0xFFFFU;
    timer_init.ClkDiv = TIM_CLK_DIV1;
    timer_init.CntMode = TIM_CNT_MODE_UP;
    timer_init.RepetCnt = 0U;
    TIM_InitTimeBase(TIM2, &timer_init);

    TIM_InitIcStruct(&capture_init);
    capture_init.Channel = TIM_CH_1;
    capture_init.IcPolarity = TIM_IC_POLARITY_RISING;
    capture_init.IcSelection = TIM_IC_SELECTION_DIRECTTI;
    capture_init.IcPrescaler = TIM_IC_PSC_DIV1;
    capture_init.IcFilter = 8U;
    TIM_ICInit(TIM2, &capture_init);

    capture_init.Channel = TIM_CH_2;
    TIM_ICInit(TIM2, &capture_init);

    TIM_SetCnt(TIM2, 0U);
    TIM_ClearFlag(TIM2,
                  TIM_FLAG_CC1 | TIM_FLAG_CC2
                  | TIM_FLAG_CC1OF | TIM_FLAG_CC2OF);
    TIM_Enable(TIM2, ENABLE);
    s_legacy_capture_initialized = 1U;
}

static void legacy_capture_irq_enable(uint16_t interrupt,
                                      uint32_t flag)
{
    NVIC_InitType nvic_init;

    legacy_capture_timer_init();
    TIM_ClearFlag(TIM2, flag);
    TIM_ConfigInt(TIM2, interrupt, ENABLE);

    nvic_init.NVIC_IRQChannel = TIM2_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 2U;
    nvic_init.NVIC_IRQChannelSubPriority = 3U;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
}

void GpioIni(void)
{
    GPIO_InitType gpio_init;

    RCC_EnableAPB2PeriphClk(IO_GPIO_CLOCKS, ENABLE);

    /* 关闭JTAG并保留SWD，释放PA15给Y4使用。 */
    GPIO_ConfigPinRemap(GPIO_RMP_SW_JTAG_SW_ENABLE, ENABLE);

    /* X1～X4对应PA0～PA3，光耦输出为开集电极、低有效。 */
    gpio_init.Pin = IO_INPUT_PINS;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpio_init);

    /* 先把输出锁存器置高，再切换为推挽输出，避免初始化时误导通。 */
    GPIO_SetBits(GPIOC, IO_OUTPUT_PC_PINS);
    GPIO_SetBits(GPIOA, IO_OUTPUT_PA_PINS | MOTOR_ERROR_LED_PIN);

    gpio_init.Pin = IO_OUTPUT_PC_PINS;
    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOC, &gpio_init);

    gpio_init.Pin = IO_OUTPUT_PA_PINS | MOTOR_ERROR_LED_PIN;
    GPIO_InitPeripheral(GPIOA, &gpio_init);

    /* PB14是低有效的VFO/驱动故障输入。 */
    gpio_init.Pin = MOTOR_VFO_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitPeripheral(GPIOB, &gpio_init);
}

u8 XiFen(void)
{
    return (u8)((io_read_raw(IO_INPUT_X1) << 3U)
                | (io_read_raw(IO_INPUT_X2) << 2U)
                | (io_read_raw(IO_INPUT_X3) << 1U)
                | io_read_raw(IO_INPUT_X4));
}

u8 DianLiu(void)
{
    return (u8)((io_read_raw(IO_INPUT_X1) << 2U)
                | (io_read_raw(IO_INPUT_X2) << 1U)
                | io_read_raw(IO_INPUT_X3));
}

void IoPuIntEnable(void)
{
    legacy_capture_irq_enable(TIM_INT_CC1, TIM_FLAG_CC1);
}

void IoDrIntEnable(void)
{
    legacy_capture_irq_enable(TIM_INT_CC2, TIM_FLAG_CC2);
}

void IointDisable(void)
{
    TIM_ConfigInt(TIM2, TIM_INT_CC1, DISABLE);
    TIM_ConfigInt(TIM2, TIM_INT_CC2, DISABLE);
    TIM_ClearFlag(TIM2, TIM_FLAG_CC1);
    TIM_ClearFlag(TIM2, TIM_FLAG_CC2);
}

void IoCaptureHandle(void)
{
    /*
     * N32G455使用TIM2捕获PA0/PA1的脉冲和方向事件。本函数是由
     * TIM2_IRQHandler()调用的公共处理过程，不是硬件中断向量。
     */
    if (TIM_GetIntStatus(TIM2, TIM_INT_CC1) != RESET)
    {
        TIM_ClrIntPendingBit(TIM2, TIM_INT_CC1);
        PuIoIntHandle();
    }

    if (TIM_GetIntStatus(TIM2, TIM_INT_CC2) != RESET)
    {
        TIM_ClrIntPendingBit(TIM2, TIM_INT_CC2);
        DrIoIntHandle();
    }
}

void TIM2_IRQHandler(void)
{
    IoCaptureHandle();

    if (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC1OF) != RESET)
    {
        TIM_ClearFlag(TIM2, TIM_FLAG_CC1OF);
    }

    if (TIM_GetFlagStatus(TIM2, TIM_FLAG_CC2OF) != RESET)
    {
        TIM_ClearFlag(TIM2, TIM_FLAG_CC2OF);
    }
}

u8 IoPuGet(void)
{
    return io_read_raw(IO_INPUT_X1);
}

u8 IoDrGet(void)
{
    return io_read_raw(IO_INPUT_X2);
}

u8 IoMfGet(void)
{
    return (u8)GPIO_ReadInputDataBit(GPIOB, MOTOR_VFO_PIN);
}

u8 IoVfoGet(void)
{
    return (u8)GPIO_ReadInputDataBit(GPIOB, MOTOR_VFO_PIN);
}

u8 IoModeGet(void)
{
    return io_read_raw(IO_INPUT_X4);
}

void LedRunSet(u8 temdata)
{
    /* 旧接口的0表示点亮；映射到低有效的Y1。 */
    io_write_output(IO_OUTPUT_Y1, (temdata == 0U) ? 1U : 0U);
}

void LedErrSet(u8 temdata)
{
    if (temdata == 0U)
    {
        GPIO_ResetBits(GPIOA, MOTOR_ERROR_LED_PIN);
    }
    else if (temdata == 1U)
    {
        GPIO_SetBits(GPIOA, MOTOR_ERROR_LED_PIN);
    }
    else
    {
        GPIOA->POD ^= MOTOR_ERROR_LED_PIN;
    }
}

uint8_t io_read_input(io_input_t input)
{
    uint32_t index;

    index = (uint32_t)input;
    if (index >= IO_INPUT_COUNT)
    {
        return 0U;
    }

    /* 光耦有效时GPIO被拉低，对外统一返回逻辑1。 */
    return (GPIO_ReadInputDataBit(s_input_ports[index],
                                  s_input_pins[index]) == 0U) ? 1U : 0U;
}

void io_write_output(io_output_t output, uint8_t active)
{
    uint32_t index;

    index = (uint32_t)output;
    if ((index >= IO_OUTPUT_COUNT) || (s_output_pins[index] == 0U))
    {
        return;
    }

    /* MCU输出低电平时，板级输出光耦导通。 */
    GPIO_WriteBit(s_output_ports[index], s_output_pins[index],
                  (active != 0U) ? Bit_RESET : Bit_SET);
}

uint8_t io_read_output(io_output_t output)
{
    uint32_t index;

    index = (uint32_t)output;
    if ((index >= IO_OUTPUT_COUNT) || (s_output_pins[index] == 0U))
    {
        return 0U;
    }

    return (GPIO_ReadOutputDataBit(s_output_ports[index],
                                   s_output_pins[index]) == 0U) ? 1U : 0U;
}
