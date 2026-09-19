#include "can.h"

static can_rx_callback_t s_rx_callback = 0;

/**
 * @brief Map a supported CAN bit rate to the configured time-base prescaler.
 * @param[in] baudrate Requested bit rate in bit/s.
 * @return CAN timing prescaler; unsupported rates select the 500 kbit/s value.
 */
static uint16_t can_get_prescaler(uint32_t baudrate)
{
    switch (baudrate)
    {
    case 250000U:
        return 8U;
    case 125000U:
        return 16U;
    default:
        return 4U;
    }
}

void can_init(uint32_t baudrate)
{
    GPIO_InitType gpio_init;
    CAN_InitType can_init;
    CAN_FilterInitType filter_init;
    NVIC_InitType nvic_init;

    /* 参数为 0 时使用默认 500 kbit/s。 */
    if (baudrate == 0U)
    {
        baudrate = 500000U;
    }

    /* 打开 CAN1 和收发引脚时钟，RX 上拉输入、TX 复用推挽输出。 */
    RCC_EnableAPB2PeriphClk(BSP_CAN_GPIO_CLK, ENABLE);
    RCC_EnableAPB1PeriphClk(BSP_CAN_CLK, ENABLE);

    gpio_init.Pin = BSP_CAN_RX_GPIO_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(BSP_CAN_RX_GPIO_PORT, &gpio_init);

    gpio_init.Pin = BSP_CAN_TX_GPIO_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitPeripheral(BSP_CAN_TX_GPIO_PORT, &gpio_init);

    /* 正常模式下开启自动离线恢复和自动唤醒，允许报文自动重发。 */
    CAN_DeInit(BSP_CAN_PORT);
    can_init.TTCM = DISABLE;
    can_init.ABOM = ENABLE;
    can_init.AWKUM = ENABLE;
    can_init.NART = DISABLE;
    can_init.RFLM = DISABLE;
    can_init.TXFP = DISABLE;
    can_init.OperatingMode = CAN_Normal_Mode;
    can_init.RSJW = BSP_CAN_TIMING_RSJW;
    can_init.TBS1 = BSP_CAN_TIMING_TBS1;
    can_init.TBS2 = BSP_CAN_TIMING_TBS2;
    can_init.BaudRatePrescaler = can_get_prescaler(baudrate);
    CAN_Init(BSP_CAN_PORT, &can_init);

    /* 当前过滤器掩码全为 0，FIFO0 接收所有标准帧和扩展帧。 */
    filter_init.Filter_Num = BSP_CAN_FILTER_NUM;
    filter_init.Filter_Mode = CAN_Filter_IdMaskMode;
    filter_init.Filter_Scale = CAN_Filter_32bitScale;
    filter_init.Filter_HighId = 0U;
    filter_init.Filter_LowId = 0U;
    filter_init.FilterMask_HighId = 0U;
    filter_init.FilterMask_LowId = 0U;
    filter_init.Filter_FIFOAssignment = CAN_Filter_FIFO0;
    filter_init.Filter_Act = ENABLE;
    CAN1_InitFilter(&filter_init);

    /* FIFO0 有新报文时进入中断，由回调函数交给上层处理。 */
    CAN_INTConfig(BSP_CAN_PORT, BSP_CAN_RX_INT, ENABLE);
    nvic_init.NVIC_IRQChannel = BSP_CAN_RX_IRQ;
    nvic_init.NVIC_IRQChannelPreemptionPriority = BSP_CAN_RX_IRQ_PREEMPT_PRIORITY;
    nvic_init.NVIC_IRQChannelSubPriority = BSP_CAN_RX_IRQ_SUB_PRIORITY;
    nvic_init.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&nvic_init);
}

void can_set_rx_callback(can_rx_callback_t callback)
{
    s_rx_callback = callback;
}

uint8_t can_send(const can_frame_t *frame)
{
    CanTxMessage message;
    uint8_t index;
    uint8_t length;

    if (frame == 0)
    {
        return 0U;
    }

    /* DLC 最大为 8，并根据帧类型选择 11 位或 29 位标识符。 */
    length = (frame->dlc > BSP_CAN_DATA_MAX_LEN) ? BSP_CAN_DATA_MAX_LEN : frame->dlc;
    message.IDE = (frame->id_type == BSP_CAN_ID_EXT) ? CAN_Extended_Id : CAN_Standard_Id;
    message.RTR = (frame->rtr == BSP_CAN_RTR_REMOTE) ? CAN_RTRQ_Remote : CAN_RTRQ_Data;
    message.DLC = length;
    message.StdId = (message.IDE == CAN_Standard_Id) ? (frame->id & 0x7FFU) : 0U;
    message.ExtId = (message.IDE == CAN_Extended_Id) ? (frame->id & 0x1FFFFFFFU) : 0U;
    for (index = 0U; index < BSP_CAN_DATA_MAX_LEN; index++)
    {
        message.Data[index] = (index < length) ? frame->data[index] : 0U;
    }

    /* 没有空闲发送邮箱时立即返回失败，不在此阻塞等待。 */
    return (CAN_TransmitMessage(BSP_CAN_PORT, &message) == CAN_TxSTS_NoMailBox)
           ? 0U : 1U;
}

uint8_t can_read(can_frame_t *frame)
{
    CanRxMessage message;
    uint8_t index;
    uint8_t length;

    if ((frame == 0) || (CAN_PendingMessage(BSP_CAN_PORT, BSP_CAN_RX_FIFO) == 0U))
    {
        return 0U;
    }

    /* 从 FIFO0 取出一帧并转换为工程统一的 can_frame_t 格式。 */
    CAN_ReceiveMessage(BSP_CAN_PORT, BSP_CAN_RX_FIFO, &message);
    length = (message.DLC > BSP_CAN_DATA_MAX_LEN) ? BSP_CAN_DATA_MAX_LEN : message.DLC;
    frame->id_type = (message.IDE == CAN_Extended_Id) ? BSP_CAN_ID_EXT : BSP_CAN_ID_STD;
    frame->rtr = (message.RTR == CAN_RTRQ_Remote) ? BSP_CAN_RTR_REMOTE : BSP_CAN_RTR_DATA;
    frame->dlc = length;
    frame->id = (frame->id_type == BSP_CAN_ID_EXT) ? message.ExtId : message.StdId;
    for (index = 0U; index < BSP_CAN_DATA_MAX_LEN; index++)
    {
        frame->data[index] = (index < length) ? message.Data[index] : 0U;
    }
    return 1U;
}

/**
 * @brief 读空 CAN1 FIFO0，并调用已注册的接收回调。
 * @warning 回调运行在中断上下文中，不能执行阻塞操作。
 */
void USB_LP_CAN1_RX0_IRQHandler(void)
{
    can_frame_t frame;

    if (CAN_GetIntStatus(BSP_CAN_PORT, BSP_CAN_RX_INT) == RESET)
    {
        return;
    }

    /* 一次中断把 FIFO 中已到达的报文全部处理完。 */
    while (can_read(&frame) != 0U)
    {
        if (s_rx_callback != 0)
        {
            s_rx_callback(&frame);
        }
    }
}
