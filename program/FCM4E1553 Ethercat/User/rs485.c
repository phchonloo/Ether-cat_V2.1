#include "rs485.h"

#define RS485_TX_DMA_CHANNEL       DMA1_CH6
#define RS485_RX_DMA_CHANNEL       DMA1_CH7
#define RS485_TX_DMA_IRQ           DMA1_Channel6_IRQn
#define RS485_TX_DMA_GLOBAL_FLAG   DMA1_INT_GLB6
#define RS485_TX_DMA_COMPLETE_FLAG DMA1_INT_TXC6
#define RS485_TX_DMA_ERROR_FLAG    DMA1_INT_ERR6

static uint8_t s_rs485_rx_dma_buffer[RS485_RX_BUFFER_SIZE];
static uint8_t s_rs485_tx_dma_buffer[RS485_TX_BUFFER_SIZE];
static volatile uint16_t s_rs485_rx_read_position = 0U;
static volatile uint16_t s_rs485_rx_frame_position = 0U;
static volatile uint16_t s_rs485_tx_length = 0U;
static volatile uint8_t s_rs485_frame_ready = 0U;
static volatile uint8_t s_rs485_tx_busy = 0U;

static volatile uint32_t s_rs485_rx_byte_count;
static volatile uint32_t s_rs485_tx_byte_count;
static volatile uint32_t s_rs485_error_count;
static volatile uint32_t s_rs485_drop_count;

/**
 * @brief 通过 PC13 控制 RS485 收发方向。
 * @param[in] transmit 非零时打开驱动发送，0 时切回接收。
 */
static void rs485_set_direction(uint8_t transmit)
{
    GPIO_WriteBit(RS485_DE_GPIO_PORT,
                  RS485_DE_GPIO_PIN,
                  (transmit != 0U) ? Bit_SET : Bit_RESET);
}

void rs485_init(uint32_t baudrate)
{
    GPIO_InitType gpio_init;
    USART_InitType usart_init;
    DMA_InitType dma_init;
    NVIC_InitType nvic_init;

    /* 波特率为 0 时使用工程默认值，避免 USART 分频参数无效。 */
    if (baudrate == 0U)
    {
        baudrate = 115200U;
    }

    /* 清空 DMA 环形缓冲区的软件位置和调试计数。 */
    s_rs485_rx_read_position = 0U;
    s_rs485_rx_frame_position = 0U;
    s_rs485_tx_length = 0U;
    s_rs485_frame_ready = 0U;
    s_rs485_tx_busy = 0U;
    s_rs485_rx_byte_count = 0U;
    s_rs485_tx_byte_count = 0U;
    s_rs485_error_count = 0U;
    s_rs485_drop_count = 0U;

    /* 打开 DMA、UART5、GPIOB/PC13 以及复用功能时钟。 */
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_DMA1, ENABLE);
    RCC_EnableAPB2PeriphClk(RS485_GPIO_CLK | RS485_DE_GPIO_CLK, ENABLE);
    RCC_EnableAPB1PeriphClk(RS485_CLK, ENABLE);
    GPIO_ConfigPinRemap(RS485_GPIO_REMAP, ENABLE);

    /* PB8 为 UART5_TX，PB9 为 UART5_RX，PC13 为外部收发器 DE。 */
    gpio_init.Pin = RS485_TX_GPIO_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(RS485_TX_GPIO_PORT, &gpio_init);

    gpio_init.Pin = RS485_RX_GPIO_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitPeripheral(RS485_RX_GPIO_PORT, &gpio_init);

    /*
     * 先把输出锁存器拉低再切换 PC13 模式，避免初始化瞬间误进入发送态。
     * DE 与 /RE 共用此引脚：低电平接收，高电平发送。
     */
    GPIO_ResetBits(RS485_DE_GPIO_PORT, RS485_DE_GPIO_PIN);
    gpio_init.Pin = RS485_DE_GPIO_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitPeripheral(RS485_DE_GPIO_PORT, &gpio_init);
    rs485_set_direction(0U);

    /* Modbus RTU 使用 8N1，无硬件流控。 */
    USART_DeInit(RS485_PORT);
    usart_init.BaudRate = baudrate;
    usart_init.WordLength = USART_WL_8B;
    usart_init.StopBits = USART_STPB_1;
    usart_init.Parity = USART_PE_NO;
    usart_init.Mode = USART_MODE_RX | USART_MODE_TX;
    usart_init.HardwareFlowControl = USART_HFCTRL_NONE;
    USART_Init(RS485_PORT, &usart_init);

    /* RX DMA 使用循环模式，持续接收；空闲中断只记录当前写入位置。 */
    DMA_DeInit(RS485_RX_DMA_CHANNEL);
    DMA_StructInit(&dma_init);
    dma_init.PeriphAddr = (uint32_t)&RS485_PORT->DAT;
    dma_init.MemAddr = (uint32_t)s_rs485_rx_dma_buffer;
    dma_init.Direction = DMA_DIR_PERIPH_SRC;
    dma_init.BufSize = RS485_RX_BUFFER_SIZE;
    dma_init.PeriphInc = DMA_PERIPH_INC_DISABLE;
    dma_init.DMA_MemoryInc = DMA_MEM_INC_ENABLE;
    dma_init.PeriphDataSize = DMA_PERIPH_DATA_SIZE_BYTE;
    dma_init.MemDataSize = DMA_MemoryDataSize_Byte;
    dma_init.CircularMode = DMA_MODE_CIRCULAR;
    dma_init.Priority = DMA_PRIORITY_HIGH;
    dma_init.Mem2Mem = DMA_M2M_DISABLE;
    DMA_Init(RS485_RX_DMA_CHANNEL, &dma_init);
    DMA_RequestRemap(DMA1_REMAP_UART5_RX,
                     DMA1,
                     RS485_RX_DMA_CHANNEL,
                     ENABLE);

    /* TX DMA 使用普通模式，每次发送前重新装入地址和字节数。 */
    DMA_DeInit(RS485_TX_DMA_CHANNEL);
    DMA_StructInit(&dma_init);
    dma_init.PeriphAddr = (uint32_t)&RS485_PORT->DAT;
    dma_init.MemAddr = (uint32_t)s_rs485_tx_dma_buffer;
    dma_init.Direction = DMA_DIR_PERIPH_DST;
    dma_init.BufSize = RS485_TX_BUFFER_SIZE;
    dma_init.PeriphInc = DMA_PERIPH_INC_DISABLE;
    dma_init.DMA_MemoryInc = DMA_MEM_INC_ENABLE;
    dma_init.PeriphDataSize = DMA_PERIPH_DATA_SIZE_BYTE;
    dma_init.MemDataSize = DMA_MemoryDataSize_Byte;
    dma_init.CircularMode = DMA_MODE_NORMAL;
    dma_init.Priority = DMA_PRIORITY_MEDIUM;
    dma_init.Mem2Mem = DMA_M2M_DISABLE;
    DMA_Init(RS485_TX_DMA_CHANNEL, &dma_init);
    DMA_RequestRemap(DMA1_REMAP_UART5_TX,
                     DMA1,
                     RS485_TX_DMA_CHANNEL,
                     ENABLE);
    DMA_ConfigInt(RS485_TX_DMA_CHANNEL,
                  DMA_INT_TXC | DMA_INT_ERR,
                  ENABLE);

    /* 空闲中断负责分帧；TXC 中断只在 DMA 搬运完成后临时打开。 */
    USART_ConfigInt(RS485_PORT, USART_INT_TXC, DISABLE);
    USART_ConfigInt(RS485_PORT, USART_INT_IDLEF, ENABLE);
    USART_EnableDMA(RS485_PORT,
                    USART_DMAREQ_RX | USART_DMAREQ_TX,
                    ENABLE);

    nvic_init.NVIC_IRQChannel = RS485_IRQ;
    nvic_init.NVIC_IRQChannelPreemptionPriority = RS485_IRQ_PREEMPT_PRIORITY;
    nvic_init.NVIC_IRQChannelSubPriority = RS485_IRQ_SUB_PRIORITY;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);

    nvic_init.NVIC_IRQChannel = RS485_TX_DMA_IRQ;
    nvic_init.NVIC_IRQChannelPreemptionPriority = RS485_DMA_IRQ_PREEMPT_PRIORITY;
    nvic_init.NVIC_IRQChannelSubPriority = RS485_DMA_IRQ_SUB_PRIORITY;
    NVIC_Init(&nvic_init);

    /* 清除可能残留的状态，再启动 RX DMA 和 UART5。 */
    (void)RS485_PORT->STS;
    (void)RS485_PORT->DAT;
    DMA_EnableChannel(RS485_RX_DMA_CHANNEL, ENABLE);
    USART_Enable(RS485_PORT, ENABLE);
}

uint8_t rs485_send(const uint8_t *data, uint16_t length)
{
    /* 拒绝空指针、空帧和超过内部发送缓冲区的帧。 */
    if ((data == 0) || (length == 0U)
        || (length > RS485_TX_BUFFER_SIZE))
    {
        return 0U;
    }

    /* 短暂屏蔽 DMA 中断，原子地检查并占用发送状态。 */
    NVIC_DisableIRQ(RS485_TX_DMA_IRQ);
    if (s_rs485_tx_busy != 0U)
    {
        NVIC_EnableIRQ(RS485_TX_DMA_IRQ);
        s_rs485_drop_count++;
        return 0U;
    }
    s_rs485_tx_busy = 1U;
    NVIC_EnableIRQ(RS485_TX_DMA_IRQ);

    /* 先复制数据，再拉高 DE，避免调用者缓冲区在 DMA 期间被修改。 */
    memcpy(s_rs485_tx_dma_buffer, data, length);
    s_rs485_tx_length = length;
    rs485_set_direction(1U);
    USART_ConfigInt(RS485_PORT, USART_INT_TXC, DISABLE);
    USART_ClrFlag(RS485_PORT, USART_FLAG_TXC);

    /* 重新装载 DMA 传输地址和长度，启动本次异步发送。 */
    DMA_EnableChannel(RS485_TX_DMA_CHANNEL, DISABLE);
    DMA_ClrIntPendingBit(RS485_TX_DMA_GLOBAL_FLAG, DMA1);
    RS485_TX_DMA_CHANNEL->MADDR = (uint32_t)s_rs485_tx_dma_buffer;
    DMA_SetCurrDataCounter(RS485_TX_DMA_CHANNEL, length);
    DMA_EnableChannel(RS485_TX_DMA_CHANNEL, ENABLE);
    return 1U;
}

uint16_t rs485_read(uint8_t *data, uint16_t capacity)
{
    uint16_t start;
    uint16_t end;
    uint16_t length;
    uint16_t index;

    if ((data == 0) || (capacity == 0U)
        || (s_rs485_frame_ready == 0U))
    {
        return 0U;
    }

    /* 与 UART5 空闲中断互斥，原子取得一帧的起止位置。 */
    NVIC_DisableIRQ(RS485_IRQ);
    start = s_rs485_rx_read_position;
    end = s_rs485_rx_frame_position;
    s_rs485_rx_read_position = end;
    s_rs485_frame_ready = 0U;
    NVIC_EnableIRQ(RS485_IRQ);

    /* 计算循环 DMA 缓冲区中本帧长度，包含回绕情况。 */
    if (end >= start)
    {
        length = (uint16_t)(end - start);
    }
    else
    {
        length = (uint16_t)(RS485_RX_BUFFER_SIZE - start + end);
    }

    if ((length == 0U) || (length > capacity))
    {
        if (length > capacity)
        {
            s_rs485_drop_count++;
        }
        return 0U;
    }

    /* 按字节复制到线性缓冲区，供上层 Modbus 解析。 */
    for (index = 0U; index < length; index++)
    {
        data[index] = s_rs485_rx_dma_buffer[start];
        start++;
        if (start == RS485_RX_BUFFER_SIZE)
        {
            start = 0U;
        }
    }
    s_rs485_rx_byte_count += length;
    return length;
}

/**
 * @brief 处理 UART5 空闲、接收错误和发送完成事件。
 * @note 空闲事件用于记录 RX DMA 写指针；最后一个停止位发完后才拉低 DE。
 */
void UART5_IRQHandler(void)
{
    uint32_t status;
    uint16_t position;

    /* 只读取一次状态寄存器，保证后续判断基于同一时刻的状态。 */
    status = RS485_PORT->STS;

    if ((status & (USART_FLAG_IDLEF | USART_FLAG_OREF
                   | USART_FLAG_NEF | USART_FLAG_FEF
                   | USART_FLAG_PEF)) != 0U)
    {
        (void)RS485_PORT->DAT;
    }

    if ((status & (USART_FLAG_OREF | USART_FLAG_NEF
                   | USART_FLAG_FEF | USART_FLAG_PEF)) != 0U)
    {
        s_rs485_error_count++;
    }

    /* 总线出现空闲时，用 DMA 剩余计数换算当前写入位置并标记一帧就绪。 */
    if ((status & USART_FLAG_IDLEF) != 0U)
    {
        position = (uint16_t)(RS485_RX_BUFFER_SIZE
                              - DMA_GetCurrDataCounter(
                                  RS485_RX_DMA_CHANNEL));
        if (position == RS485_RX_BUFFER_SIZE)
        {
            position = 0U;
        }
        if (position != s_rs485_rx_read_position)
        {
            s_rs485_rx_frame_position = position;
            s_rs485_frame_ready = 1U;
        }
    }

    /* UART 移位寄存器真正发送完毕后才能关闭驱动，防止截断最后一个字节。 */
    if (USART_GetIntStatus(RS485_PORT, USART_INT_TXC) != RESET)
    {
        USART_ConfigInt(RS485_PORT, USART_INT_TXC, DISABLE);
        USART_ClrIntPendingBit(RS485_PORT, USART_INT_TXC);
        rs485_set_direction(0U);
        s_rs485_tx_byte_count += s_rs485_tx_length;
        s_rs485_tx_length = 0U;
        s_rs485_tx_busy = 0U;
    }
}

/**
 * @brief 处理 UART5 发送 DMA 完成或错误。
 * @note DMA 完成只代表数据搬入 UART，不能立即拉低 DE，仍需等待 UART TXC。
 */
void DMA1_Channel6_IRQHandler(void)
{
    /* DMA 出错时立即停止发送并回到接收状态，防止 DE 一直保持高电平。 */
    if (DMA_GetIntStatus(RS485_TX_DMA_ERROR_FLAG, DMA1) != RESET)
    {
        DMA_ClrIntPendingBit(RS485_TX_DMA_GLOBAL_FLAG, DMA1);
        DMA_EnableChannel(RS485_TX_DMA_CHANNEL, DISABLE);
        USART_ConfigInt(RS485_PORT, USART_INT_TXC, DISABLE);
        rs485_set_direction(0U);
        s_rs485_tx_length = 0U;
        s_rs485_tx_busy = 0U;
        s_rs485_error_count++;
        return;
    }

    /* DMA 搬运结束后打开 UART TXC 中断，等待物理线路上的最后一位发完。 */
    if (DMA_GetIntStatus(RS485_TX_DMA_COMPLETE_FLAG, DMA1) != RESET)
    {
        DMA_ClrIntPendingBit(RS485_TX_DMA_GLOBAL_FLAG, DMA1);
        DMA_EnableChannel(RS485_TX_DMA_CHANNEL, DISABLE);
        USART_ConfigInt(RS485_PORT, USART_INT_TXC, ENABLE);
    }
}
