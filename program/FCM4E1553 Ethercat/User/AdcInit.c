#include "AdcInit.h"

u32 adc_value[10]={0,0,0,0,0,0,0,0,0,0};
/**
    \brief      configure the GPIO ports
    \param[in]  none
    \param[out] none
    \retval     none
  */

#define ADC_SAMPLETIMESET ADC_SAMP_TIME_28CYCLES5

void dma_config(void)
{
    /* ADC_DMA_channel configuration */
    DMA_InitType dma_data_parameter;
    NVIC_InitType nvic_init;

    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_DMA2, ENABLE);

    /* ADC DMA_channel configuration */
    DMA_DeInit(DMA2_CH1);

    /* initialize DMA single data mode */
    DMA_StructInit(&dma_data_parameter);
    dma_data_parameter.PeriphAddr  = (uint32_t)(&ADC3->DAT);
    dma_data_parameter.PeriphInc   = DMA_PERIPH_INC_DISABLE;
    dma_data_parameter.MemAddr     = (uint32_t)(adc_value);
    dma_data_parameter.DMA_MemoryInc = DMA_MEM_INC_ENABLE;
    dma_data_parameter.PeriphDataSize = DMA_PERIPH_DATA_SIZE_HALFWORD;
    dma_data_parameter.MemDataSize = DMA_MemoryDataSize_Word;
    dma_data_parameter.Direction   = DMA_DIR_PERIPH_SRC;
    dma_data_parameter.BufSize     = 10U;
    dma_data_parameter.Priority    = DMA_PRIORITY_HIGH;
    dma_data_parameter.CircularMode = DMA_MODE_CIRCULAR;
    dma_data_parameter.Mem2Mem     = DMA_M2M_DISABLE;
    DMA_Init(DMA2_CH1, &dma_data_parameter);

    DMA_RequestRemap(DMA2_REMAP_ADC3, DMA2, DMA2_CH1, ENABLE);
    DMA_ClrIntPendingBit(DMA2_INT_GLB1, DMA2);
    DMA_ConfigInt(DMA2_CH1, DMA_INT_TXC | DMA_INT_ERR, ENABLE);

    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    nvic_init.NVIC_IRQChannel = DMA2_Channel1_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 0U;
    nvic_init.NVIC_IRQChannelSubPriority = 0U;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);

    /* enable DMA channel */
    DMA_EnableChannel(DMA2_CH1, ENABLE);
}

/*!
    \brief      configure the ADC peripheral
    \param[in]  none
    \param[out] none
    \retval     none
*/
void adc_config(void)
{
    GPIO_InitType gpio_init;
    ADC_InitType adc_parameter;
    uint32_t timeout;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOB
                            | RCC_APB2_PERIPH_GPIOE, ENABLE);
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_ADC3, ENABLE);

    gpio_init.Pin = GPIO_PIN_11 | GPIO_PIN_13;
    gpio_init.GPIO_Mode = GPIO_Mode_AIN;
    gpio_init.GPIO_Speed = GPIO_INPUT;
    GPIO_InitPeripheral(GPIOB, &gpio_init);

    gpio_init.Pin = GPIO_PIN_12 | GPIO_PIN_13;
    GPIO_InitPeripheral(GPIOE, &gpio_init);

    ADC_ConfigClk(ADC_CTRL3_CKMOD_AHB, RCC_ADCHCLK_DIV16);
    RCC_ConfigAdc1mClk(RCC_ADC1MCLK_SRC_HSE, RCC_ADC1MCLK_DIV8);

    ADC_DeInit(ADC3);
    ADC_InitStruct(&adc_parameter);
    adc_parameter.WorkMode = ADC_WORKMODE_INDEPENDENT;
    adc_parameter.MultiChEn = ENABLE;
    adc_parameter.ContinueConvEn = DISABLE;
    adc_parameter.ExtTrigSelect = ADC_EXT_TRIGCONV_T8_TRGO;
    adc_parameter.DatAlign = ADC_DAT_ALIGN_R;
    adc_parameter.ChsNumber = 10U;
    ADC_Init(ADC3, &adc_parameter);

    /* ADC regular channel config */
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_01_PB11, 1U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_05_PB13, 2U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_04_PE12, 3U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_03_PE13, 4U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_01_PB11, 5U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_05_PB13, 6U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_01_PB11, 7U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_05_PB13, 8U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_01_PB11, 9U, ADC_SAMPLETIMESET);
    ADC_ConfigRegularChannel(ADC3, ADC3_Channel_05_PB13, 10U, ADC_SAMPLETIMESET);

    ADC_EnableExternalTrigConv(ADC3, ENABLE);

    /* enable ADC interface */
    ADC_Enable(ADC3, ENABLE);
    ADC_StartCalibration(ADC3);
    timeout = 0x000FFFFFU;
    while ((ADC_GetCalibrationStatus(ADC3) == SET) && (timeout > 0U))
    {
        timeout--;
    }

    if (ADC_GetCalibrationStatus(ADC3) != RESET)
    {
        ADC_Enable(ADC3, DISABLE);
        return;
    }

    /* ADC DMA function enable */
    ADC_ClearIntPendingBit(ADC3, ADC_INT_ENDC);
    ADC_EnableDMA(ADC3, ENABLE);
}

void DMA2_Channel1_IRQHandler(void)
{
    if (DMA_GetIntStatus(DMA2_INT_ERR1, DMA2) != RESET)
    {
        DMA_ClrIntPendingBit(DMA2_INT_GLB1, DMA2);
        return;
    }

    if (DMA_GetIntStatus(DMA2_INT_TXC1, DMA2) != RESET)
    {
        DMA_ClrIntPendingBit(DMA2_INT_GLB1, DMA2);
        //GPIOC->POD ^= GPIO_PIN_10;
    }
}
