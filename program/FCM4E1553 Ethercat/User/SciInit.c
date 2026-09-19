#include "SciInit.h"

#define UART_TX_DMA_CHANNEL DMA1_CH4
#define UART_RX_DMA_CHANNEL DMA1_CH5
#define UART_TX_DMA_IRQ     DMA1_Channel4_IRQn

u16 CrcH;
u16 CrcL;
u8 RxSci_i = 0U;
u16 RxSciData[8] = {1U, 0U, 3U, 0U, 5U, 0U, 8U, 0U};
u8 RxSciData_i = 0U;

static volatile uint32_t s_uart_rx_count;
static volatile uint32_t s_uart_tx_count;
static volatile uint32_t s_uart_drop_count;
static volatile uint8_t s_uart_last_byte;

static uint8_t s_uart_rx_dma_buffer[UART_BUFFER_SIZE];
static uint8_t s_uart_rx_queue[UART_BUFFER_SIZE];
static uint8_t s_uart_tx_buffer[UART_BUFFER_SIZE];
static volatile uint16_t s_uart_rx_position = 0U;
static volatile uint16_t s_uart_rx_head = 0U;
static volatile uint16_t s_uart_rx_tail = 0U;
static volatile uint16_t s_uart_tx_head = 0U;
static volatile uint16_t s_uart_tx_tail = 0U;
static volatile uint16_t s_uart_tx_dma_length = 0U;
static uint8_t s_uart_gpio_ready = 0U;

//u8  SciRxBuf[SCI_RX_BUF_SIZE];
//volatile u16 SciRxLen  = 0;      /* 最近一帧长度（空闲中断里更新） */
//volatile u8  SciRxFlag = 0;      /* 1 = 收到一帧待处理 */

//static u8 SciTxBuf[8];           /* 发送缓冲：SendFunction 每次发 8 字节 */
//static u8 SciTxStarted = 0;      /* TX DMA 是否已启动过（首帧免等待） */


/**
 * @brief 从 USART1 回显环形队列启动一段连续的 DMA 发送。
 * @note DMA 正忙或队列为空时直接返回。
 */
static void uart_start_tx_dma(void)
{
    uint16_t length;

    /* 同一时刻只允许一个 DMA 段在发送。 */
    if ((s_uart_tx_dma_length != 0U)
        || (s_uart_tx_tail == s_uart_tx_head))
    {
        return;
    }

    /* DMA 一次只发送到环形缓冲区末尾，不跨越回绕点。 */
    if (s_uart_tx_head > s_uart_tx_tail)
    {
        length = (uint16_t)(s_uart_tx_head - s_uart_tx_tail);
    }
    else
    {
        length = (uint16_t)(UART_BUFFER_SIZE - s_uart_tx_tail);
    }

    /* 重新装入当前连续段的首地址和长度。 */
    s_uart_tx_dma_length = length;
    DMA_EnableChannel(UART_TX_DMA_CHANNEL, DISABLE);
    DMA_ClearFlag(DMA1_INT_GLB4, DMA1);
    UART_TX_DMA_CHANNEL->MADDR =
        (uint32_t)&s_uart_tx_buffer[s_uart_tx_tail];
    DMA_SetCurrDataCounter(UART_TX_DMA_CHANNEL, length);
    DMA_EnableChannel(UART_TX_DMA_CHANNEL, ENABLE);
}

/**
 * @brief 把 RX 循环 DMA 新收到的数据复制到前台接收队列。
 * @note 在 USART1 空闲事件后调用；队列已满时丢弃数据并累加统计值。
 */
static void uart_queue_received_data(void)
{
    uint16_t position;
    uint16_t next_rx_head;
    uint8_t received;

    /* DMA 剩余计数可换算出当前写指针。 */
    position = (uint16_t)(UART_BUFFER_SIZE
                          - DMA_GetCurrDataCounter(UART_RX_DMA_CHANNEL));
    if (position == UART_BUFFER_SIZE)
    {
        position = 0U;
    }

    /* 从上次软件读指针一直处理到 DMA 当前写指针。 */
    while (s_uart_rx_position != position)
    {
        received = s_uart_rx_dma_buffer[s_uart_rx_position];
        s_uart_last_byte = received;
        s_uart_rx_count++;

        next_rx_head = (uint16_t)((s_uart_rx_head + 1U) % UART_BUFFER_SIZE);
        if (next_rx_head == s_uart_rx_tail)
        {
            s_uart_drop_count++;
        }
        else
        {
            s_uart_rx_queue[s_uart_rx_head] = received;
            s_uart_rx_head = next_rx_head;
        }

        s_uart_rx_position =
            (uint16_t)((s_uart_rx_position + 1U) % UART_BUFFER_SIZE);
    }
}

void com_gpio_init(void)
{
    GPIO_InitType gpio_init;

    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_GPIOA |
                            RCC_APB2_PERIPH_AFIO,
                            ENABLE);

    GPIO_ConfigPinRemap(GPIO_RMP_USART1, DISABLE);

    /* PA9为推挽复用TX，PA10为上拉输入RX。 */
    gpio_init.Pin = GPIO_PIN_9;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(GPIOA, &gpio_init);

    gpio_init.Pin = GPIO_PIN_10;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitPeripheral(GPIOA, &gpio_init);

    s_uart_gpio_ready = 1U;
}

void uart_init(uint32_t baudrate)
{
    USART_InitType usart_init;
    DMA_InitType dma_init;
    NVIC_InitType nvic_init;

    /* 参数为 0 时使用默认波特率。 */
    if (baudrate == 0U)
    {
        baudrate = 115200U;
    }

    /* 清空回显队列位置和调试统计变量。 */
    s_uart_rx_position = 0U;
    s_uart_rx_head = 0U;
    s_uart_rx_tail = 0U;
    s_uart_tx_head = 0U;
    s_uart_tx_tail = 0U;
    s_uart_tx_dma_length = 0U;
    s_uart_rx_count = 0U;
    s_uart_tx_count = 0U;
    s_uart_drop_count = 0U;
    s_uart_last_byte = 0U;

    /* 打开DMA1和USART1时钟，GPIO由com_gpio_init()配置。 */
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_DMA1, ENABLE);
    RCC_EnableAPB2PeriphClk(RCC_APB2_PERIPH_USART1,
                            ENABLE);

    if (s_uart_gpio_ready == 0U)
    {
        com_gpio_init();
    }

    /* 串口调试回显采用 8N1、无流控。 */
    USART_DeInit(USART1);
    usart_init.BaudRate = baudrate;
    usart_init.WordLength = USART_WL_8B;
    usart_init.StopBits = USART_STPB_1;
    usart_init.Parity = USART_PE_NO;
    usart_init.Mode = USART_MODE_RX | USART_MODE_TX;
    usart_init.HardwareFlowControl = USART_HFCTRL_NONE;
    USART_Init(USART1, &usart_init);

    /* RX 使用循环 DMA，持续接收而不逐字节进入中断。 */
    DMA_DeInit(UART_RX_DMA_CHANNEL);
    dma_init.PeriphAddr = (uint32_t)&USART1->DAT;
    dma_init.MemAddr = (uint32_t)s_uart_rx_dma_buffer;
    dma_init.Direction = DMA_DIR_PERIPH_SRC;
    dma_init.BufSize = UART_BUFFER_SIZE;
    dma_init.PeriphInc = DMA_PERIPH_INC_DISABLE;
    dma_init.DMA_MemoryInc = DMA_MEM_INC_ENABLE;
    dma_init.PeriphDataSize = DMA_PERIPH_DATA_SIZE_BYTE;
    dma_init.MemDataSize = DMA_MemoryDataSize_Byte;
    dma_init.CircularMode = DMA_MODE_CIRCULAR;
    dma_init.Priority = DMA_PRIORITY_HIGH;
    dma_init.Mem2Mem = DMA_M2M_DISABLE;
    DMA_Init(UART_RX_DMA_CHANNEL, &dma_init);
    DMA_RequestRemap(DMA1_REMAP_USART1_RX,
                     DMA1,
                     UART_RX_DMA_CHANNEL,
                     ENABLE);

    /* TX 使用普通 DMA，每个连续队列段发送完成后由中断推进 tail。 */
    DMA_DeInit(UART_TX_DMA_CHANNEL);
    dma_init.MemAddr = (uint32_t)s_uart_tx_buffer;
    dma_init.Direction = DMA_DIR_PERIPH_DST;
    dma_init.BufSize = 0U;
    dma_init.CircularMode = DMA_MODE_NORMAL;
    dma_init.Priority = DMA_PRIORITY_MEDIUM;
    DMA_Init(UART_TX_DMA_CHANNEL, &dma_init);
    DMA_RequestRemap(DMA1_REMAP_USART1_TX,
                     DMA1,
                     UART_TX_DMA_CHANNEL,
                     ENABLE);
    DMA_ConfigInt(UART_TX_DMA_CHANNEL, DMA_INT_TXC, ENABLE);

    /* 串口空闲中断负责通知软件“当前一批数据已接收完”。 */
    USART_ConfigInt(USART1, USART_INT_IDLEF, ENABLE);
    USART_EnableDMA(USART1, USART_DMAREQ_RX | USART_DMAREQ_TX, ENABLE);

    nvic_init.NVIC_IRQChannel = USART1_IRQn;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 3U;
    nvic_init.NVIC_IRQChannelSubPriority = 0U;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);

    nvic_init.NVIC_IRQChannel = UART_TX_DMA_IRQ;
    nvic_init.NVIC_IRQChannelPreemptionPriority = 3U;
    nvic_init.NVIC_IRQChannelSubPriority = 1U;
    NVIC_Init(&nvic_init);

    DMA_EnableChannel(UART_RX_DMA_CHANNEL, ENABLE);
    USART_Enable(USART1, ENABLE);
}

void com_usart_init(void)
{
    /* 与STD245S配置保持一致：PA9/PA10，4800 bit/s，8N1。 */
    uart_init(4800U);
}

uint16_t uart_write(const uint8_t *data, uint16_t length)
{
    uint16_t written;
    uint16_t next_head;

    if (data == 0)
    {
        return 0U;
    }

    NVIC_DisableIRQ(USART1_IRQn);
    NVIC_DisableIRQ(UART_TX_DMA_IRQ);

    written = 0U;
    while (written < length)
    {
        next_head = (uint16_t)((s_uart_tx_head + 1U) % UART_BUFFER_SIZE);
        if (next_head == s_uart_tx_tail)
        {
            break;
        }

        s_uart_tx_buffer[s_uart_tx_head] = data[written];
        s_uart_tx_head = next_head;
        written++;
    }

    uart_start_tx_dma();
    NVIC_EnableIRQ(UART_TX_DMA_IRQ);
    NVIC_EnableIRQ(USART1_IRQn);

    s_uart_drop_count += (uint32_t)(length - written);
    return written;
}

uint16_t uart_read(uint8_t *data, uint16_t capacity)
{
    uint16_t length;

    if (data == 0)
    {
        return 0U;
    }

    NVIC_DisableIRQ(USART1_IRQn);
    length = 0U;
    while ((length < capacity) && (s_uart_rx_tail != s_uart_rx_head))
    {
        data[length] = s_uart_rx_queue[s_uart_rx_tail];
        s_uart_rx_tail = (uint16_t)((s_uart_rx_tail + 1U) % UART_BUFFER_SIZE);
        length++;
    }
    NVIC_EnableIRQ(USART1_IRQn);
    return length;
}

void uart_process(void)
{
    /* 主循环只负责尝试启动命令响应发送，不在这里等待 DMA 完成。 */
    uart_start_tx_dma();
}

void RAMRUN SendFunction(u16 indata[])
{
    uint8_t bytes[8];
    uint32_t index;

    if (indata == 0)
    {
        return;
    }

    for (index = 0U; index < 8U; index++)
    {
        bytes[index] = (uint8_t)indata[index];
    }
    (void)uart_write(bytes, (uint16_t)sizeof(bytes));
}

void RAMRUN CrcCal(u16 indata[])
{
    /* 保留STD245S中的保密协议算法入口。 */
    (void)indata;
}

void RAMRUN RWFunction(u16 indata[])
{
    /* 保留STD245S中的保密读写处理入口。 */
    (void)indata;
}

void SciReceive(void)
{
    uint8_t received[32];
    uint16_t length;
    uint16_t index;

    length = uart_read(received, (uint16_t)sizeof(received));
    for (index = 0U; index < length; index++)
    {
        RxSciData[RxSciData_i] = received[index];
        RxSciData_i++;
        RxSci_i++;

        if (RxSciData_i >= 8U)
        {
            RxSciData_i = 0U;
            if (RxSciData[0] == 1U)
            {
                CrcCal(RxSciData);
                if ((CrcH == RxSciData[7])
                    && (CrcL == RxSciData[6]))
                {
                    RWFunction(RxSciData);
                }
            }
        }
    }

//			u8 i;

//	if(SciRxFlag)
//	{
//		SciRxFlag = 0;

//		/* 固定 8 字节帧、帧头必须为 1 */
//		if(SciRxLen >= 8 && SciRxBuf[0] == 1)
//		{
//			for(i=0;i<8;i++)
//			{
//				RxSciData[i] = SciRxBuf[i];
//			}

//			CrcCal(RxSciData);
//			if(CrcH==RxSciData[7] && CrcL==RxSciData[6])		//验证校验码
//			{
//				RWFunction(RxSciData);
//			}
//		}
//	}

    uart_process();
}

/**
 * @brief 处理 USART1 空闲中断，把新收到的数据加入回显队列。
 */
void USART1_IRQHandler(void)
{
    if (USART_GetIntStatus(USART1, USART_INT_IDLEF) != RESET)
    {
        /* 按芯片要求依次读取 STS 和 DAT 清除 IDLE 标志。 */
        (void)USART1->STS;
        (void)USART1->DAT;
        uart_queue_received_data();
        uart_start_tx_dma();
    }
}

/**
 * @brief 结束一个 USART1 TX DMA 段并继续发送下一段。
 */
void DMA1_Channel4_IRQHandler(void)
{
    if (DMA_GetIntStatus(DMA1_INT_TXC4, DMA1) != RESET)
    {
        /* 推进发送尾指针并清除忙标记，然后尝试发送队列剩余内容。 */
        DMA_ClrIntPendingBit(DMA1_INT_TXC4, DMA1);
        DMA_EnableChannel(UART_TX_DMA_CHANNEL, DISABLE);
        s_uart_tx_tail = (uint16_t)((s_uart_tx_tail + s_uart_tx_dma_length)
                                    % UART_BUFFER_SIZE);
        s_uart_tx_count += s_uart_tx_dma_length;
        s_uart_tx_dma_length = 0U;
        uart_start_tx_dma();
    }
}
