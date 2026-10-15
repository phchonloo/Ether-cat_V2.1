#include "exti.h"

/**
 * @brief Configure one active-low EtherCAT external interrupt input.
 */
static void exti_configure(uint16_t pin,
                           uint8_t pin_source,
                           uint32_t line,
                           IRQn_Type irq,
                           uint8_t preempt_priority,
                           uint8_t sub_priority)
{
    EXTI_InitType exti_init;
    GPIO_InitType gpio_init;
    NVIC_InitType nvic_init;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB |
                            RCC_APB2_PERIPH_AFIO,
                            ENABLE);

    gpio_init.Pin = pin;
    gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOB, &gpio_init);
    GPIO_ConfigEXTILine(GPIOB_PORT_SOURCE, pin_source);

    exti_init.EXTI_Line = line;
    exti_init.EXTI_Mode = EXTI_Mode_Interrupt;
    exti_init.EXTI_Trigger = EXTI_Trigger_Falling;
    exti_init.EXTI_LineCmd = ENABLE;
    EXTI_InitPeripheral(&exti_init);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    nvic_init.NVIC_IRQChannel = irq;
    nvic_init.NVIC_IRQChannelPreemptionPriority =
        preempt_priority;
    nvic_init.NVIC_IRQChannelSubPriority = sub_priority;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
}

void exti_init_irq(void)
{
    /* Reserve preemption level 0 for ESC IRQ; normal ISRs cannot preempt it.
     * PRIMASK critical sections still defer entry, and NMI/HardFault remain. */
    exti_configure(GPIO_PIN_0,
                   GPIO_PIN_SOURCE0,
                   EXTI_LINE0,
                   EXTI0_IRQn,
                   0U,
                   0U);
}

void exti_init_sync0(void)
{
    exti_configure(GPIO_PIN_1,
                   GPIO_PIN_SOURCE1,
                   EXTI_LINE1,
                   EXTI1_IRQn,
                   1U,
                   0U);
}

void exti_init_sync1(void)
{
#if ECAT_SYNC1_IRQ_ENABLED
    exti_configure(GPIO_PIN_2,
                   GPIO_PIN_SOURCE2,
                   EXTI_LINE2,
                   EXTI2_IRQn,
                   1U,
                   2U);
#else
    /* Do not claim/configure the unconnected PB2 pin as a floating input. */
    NVIC_DisableIRQ(EXTI2_IRQn);
    EXTI->IMASK &= ~EXTI_LINE2;
    EXTI->EMASK &= ~EXTI_LINE2;
    EXTI->RT_CFG &= ~EXTI_LINE2;
    EXTI->FT_CFG &= ~EXTI_LINE2;
    EXTI_ClrITPendBit(EXTI_LINE2);
    NVIC_ClearPendingIRQ(EXTI2_IRQn);
#endif
}
