#include "mcuinit.h"

static uint8_t s_modbus_slave_addr = MODBUS_DEFAULT_SLAVE_ADDR;
static uint8_t s_modbus_rx_buf[MODBUS_RX_BUF_SIZE];
static uint8_t s_modbus_chunk_buf[MODBUS_RX_BUF_SIZE];
static uint8_t s_modbus_tx_buf[MODBUS_TX_BUF_SIZE];
static uint16_t s_modbus_holding_regs[MODBUS_HOLDING_REG_COUNT];
static uint16_t s_modbus_rx_length = 0U;

static volatile uint32_t s_modbus_rx_frame_count;
static volatile uint32_t s_modbus_tx_frame_count;
static volatile uint32_t s_modbus_crc_error_count;
static volatile uint32_t s_modbus_exception_count;
static volatile uint32_t s_modbus_drop_count;

static uint8_t modbus_check_crc(const uint8_t *frame, uint16_t length);
static void modbus_handle_frame(const uint8_t *frame, uint16_t length);
static void modbus_read_holding(const uint8_t *frame, uint16_t length);
static void modbus_write_single(const uint8_t *frame, uint16_t length);
static void modbus_write_multiple(const uint8_t *frame, uint16_t length);
static void modbus_send_frame(uint16_t length);
static void modbus_send_exception(uint8_t slave_addr,
                                  uint8_t function,
                                  uint8_t exception);
static uint16_t modbus_read_u16(const uint8_t *data);
static void modbus_write_u16(uint8_t *data, uint16_t value);
static uint8_t modbus_range_is_valid(uint16_t start, uint16_t count);
static uint16_t modbus_expected_length(const uint8_t *frame,
                                       uint16_t length);

void modbus_init(uint8_t slave_addr, uint32_t baudrate)
{
    /* 地址 0 是广播地址，从站自身地址必须位于 1~247。 */
    if ((slave_addr == MODBUS_BROADCAST_ADDR)
        || (slave_addr > MODBUS_MAX_SLAVE_ADDR))
    {
        slave_addr = MODBUS_DEFAULT_SLAVE_ADDR;
    }
    if (baudrate == 0U)
    {
        baudrate = MODBUS_DEFAULT_BAUDRATE;
    }

    /* 清空帧缓存、保持寄存器和调试计数，再初始化 RS485 物理层。 */
    s_modbus_slave_addr = slave_addr;
    s_modbus_rx_length = 0U;
    memset(s_modbus_holding_regs, 0, sizeof(s_modbus_holding_regs));
    s_modbus_rx_frame_count = 0U;
    s_modbus_tx_frame_count = 0U;
    s_modbus_crc_error_count = 0U;
    s_modbus_exception_count = 0U;
    s_modbus_drop_count = 0U;
    rs485_init(baudrate);
}

void modbus_process(void)
{
    uint16_t chunk_length;
    uint16_t expected_length;

    /* 从 RS485 空闲分帧层取出一批新字节，追加到 Modbus 累积缓冲区。 */
    chunk_length = rs485_read(s_modbus_chunk_buf, MODBUS_RX_BUF_SIZE);
    if (chunk_length != 0U)
    {
        /* 剩余空间不足时丢弃当前累积帧，防止越界写入。 */
        if (chunk_length > (MODBUS_RX_BUF_SIZE - s_modbus_rx_length))
        {
            s_modbus_rx_length = 0U;
            s_modbus_drop_count++;
            return;
        }
        memcpy(&s_modbus_rx_buf[s_modbus_rx_length],
               s_modbus_chunk_buf,
               chunk_length);
        s_modbus_rx_length += chunk_length;
    }

    if (s_modbus_rx_length == 0U)
    {
        return;
    }

    /* 根据功能码和报文字段判断一帧应有的完整长度。 */
    expected_length =
        modbus_expected_length(s_modbus_rx_buf, s_modbus_rx_length);
    if ((expected_length == 0U)
        || (s_modbus_rx_length < expected_length))
    {
        return;
    }

    /* CRC 不正确时整帧丢弃，不向主站返回异常响应。 */
    if (modbus_check_crc(s_modbus_rx_buf, expected_length) == 0U)
    {
        s_modbus_rx_length = 0U;
        s_modbus_crc_error_count++;
        return;
    }

    /* CRC 正确后再分发功能码；处理完把后续粘包字节前移。 */
    s_modbus_rx_frame_count++;
    modbus_handle_frame(s_modbus_rx_buf, expected_length);
    s_modbus_rx_length -= expected_length;
    if (s_modbus_rx_length != 0U)
    {
        memmove(s_modbus_rx_buf,
                &s_modbus_rx_buf[expected_length],
                s_modbus_rx_length);
    }
}

uint8_t modbus_get_holding(uint16_t address, uint16_t *value)
{
    if ((value == 0) || (address >= MODBUS_HOLDING_REG_COUNT))
    {
        return 0U;
    }

    *value = s_modbus_holding_regs[address];
    return 1U;
}

uint8_t modbus_set_holding(uint16_t address, uint16_t value)
{
    if (address >= MODBUS_HOLDING_REG_COUNT)
    {
        return 0U;
    }

    s_modbus_holding_regs[address] = value;
    return 1U;
}

uint16_t modbus_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc;
    uint16_t index;
    uint8_t bit_index;

    /* Modbus RTU CRC16 初值为 0xFFFF，多项式反向表示为 0xA001。 */
    crc = 0xFFFFU;
    if (data == 0)
    {
        return crc;
    }

    /* 每个字节低位优先参与 8 次移位运算。 */
    for (index = 0U; index < length; index++)
    {
        crc ^= data[index];
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

/**
 * @brief Verify the low-byte-first CRC appended to one RTU frame.
 * @param[in] frame Complete RTU frame including its CRC bytes.
 * @param[in] length Complete frame length in bytes.
 * @return 1 when the CRC matches; otherwise 0.
 */
static uint8_t modbus_check_crc(const uint8_t *frame, uint16_t length)
{
    uint16_t received_crc;
    uint16_t calculated_crc;

    if ((frame == 0) || (length < MODBUS_MIN_FRAME_SIZE))
    {
        return 0U;
    }

    /* RTU 在线上传输 CRC 低字节在前、高字节在后。 */
    received_crc = (uint16_t)frame[length - 2U]
                   | ((uint16_t)frame[length - 1U] << 8U);
    calculated_crc = modbus_crc16(frame,
                                  (uint16_t)(length - MODBUS_CRC_SIZE));
    return (received_crc == calculated_crc) ? 1U : 0U;
}

/**
 * @brief Dispatch one validated RTU request to its function-code handler.
 * @param[in] frame CRC-validated request frame.
 * @param[in] length Complete request length in bytes.
 */
static void modbus_handle_frame(const uint8_t *frame, uint16_t length)
{
    uint8_t slave_addr;
    uint8_t function;

    slave_addr = frame[0];
    function = frame[1];
    /* 只处理本站地址和广播地址，其余从站报文静默忽略。 */
    if ((slave_addr != s_modbus_slave_addr)
        && (slave_addr != MODBUS_BROADCAST_ADDR))
    {
        return;
    }

    /* 仅实现 03、06、10，其他功能码返回“非法功能”。 */
    switch (function)
    {
    case MODBUS_FUNC_READ_HOLDING:
        modbus_read_holding(frame, length);
        break;

    case MODBUS_FUNC_WRITE_SINGLE:
        modbus_write_single(frame, length);
        break;

    case MODBUS_FUNC_WRITE_MULTIPLE:
        modbus_write_multiple(frame, length);
        break;

    default:
        modbus_send_exception(slave_addr,
                              function,
                              MODBUS_EX_ILLEGAL_FUNCTION);
        break;
    }
}

/**
 * @brief Execute function 0x03 (Read Holding Registers).
 * @param[in] frame Validated request frame.
 * @param[in] length Complete request length in bytes.
 */
static void modbus_read_holding(const uint8_t *frame, uint16_t length)
{
    uint16_t start;
    uint16_t count;
    uint16_t index;
    uint16_t tx_index;

    /* 广播请求不允许读取，因为总线上不能有任何从站响应。 */
    if (frame[0] == MODBUS_BROADCAST_ADDR)
    {
        return;
    }
    if (length != MODBUS_REQUEST_FIXED_SIZE)
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_VALUE);
        return;
    }

    /* 请求字段均为大端：起始地址在前，寄存器数量在后。 */
    start = modbus_read_u16(&frame[2]);
    count = modbus_read_u16(&frame[4]);
    if ((count == 0U) || (count > MODBUS_MAX_READ_REG_COUNT))
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_VALUE);
        return;
    }
    if (modbus_range_is_valid(start, count) == 0U)
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_ADDRESS);
        return;
    }

    /* 03 响应格式：地址、功能码、字节数、寄存器数据、CRC。 */
    s_modbus_tx_buf[0] = frame[0];
    s_modbus_tx_buf[1] = MODBUS_FUNC_READ_HOLDING;
    s_modbus_tx_buf[2] = (uint8_t)(count * 2U);
    tx_index = 3U;
    for (index = 0U; index < count; index++)
    {
        modbus_write_u16(&s_modbus_tx_buf[tx_index],
                         s_modbus_holding_regs[start + index]);
        tx_index += 2U;
    }
    modbus_send_frame(tx_index);
}

/**
 * @brief Execute function 0x06 (Write Single Register).
 * @param[in] frame Validated request frame.
 * @param[in] length Complete request length in bytes.
 */
static void modbus_write_single(const uint8_t *frame, uint16_t length)
{
    uint16_t address;
    uint16_t value;

    if (length != MODBUS_REQUEST_FIXED_SIZE)
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_VALUE);
        return;
    }

    /* 06 请求包含一个寄存器地址和一个 16 位写入值。 */
    address = modbus_read_u16(&frame[2]);
    value = modbus_read_u16(&frame[4]);
    if (modbus_set_holding(address, value) == 0U)
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_ADDRESS);
        return;
    }

    /* 非广播请求按协议原样回显地址、功能码、寄存器和值。 */
    if (frame[0] != MODBUS_BROADCAST_ADDR)
    {
        memcpy(s_modbus_tx_buf,
               frame,
               MODBUS_REQUEST_FIXED_SIZE - MODBUS_CRC_SIZE);
        modbus_send_frame(MODBUS_REQUEST_FIXED_SIZE - MODBUS_CRC_SIZE);
    }
}

/**
 * @brief Execute function 0x10 (Write Multiple Registers).
 * @param[in] frame Validated request frame.
 * @param[in] length Complete request length in bytes.
 */
static void modbus_write_multiple(const uint8_t *frame, uint16_t length)
{
    uint16_t start;
    uint16_t count;
    uint16_t index;
    uint16_t expected_length;

    if (length < MODBUS_WRITE_MULTIPLE_MIN_SIZE)
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_VALUE);
        return;
    }

    /* 10 请求包含起始地址、数量、字节数以及连续寄存器数据。 */
    start = modbus_read_u16(&frame[2]);
    count = modbus_read_u16(&frame[4]);
    if ((count == 0U) || (count > MODBUS_MAX_WRITE_REG_COUNT))
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_VALUE);
        return;
    }

    /* 总长度 = 7 字节头 + 2*寄存器数量 + 2 字节 CRC。 */
    expected_length = (uint16_t)(9U + (count * 2U));
    if ((frame[6] != (uint8_t)(count * 2U))
        || (length != expected_length))
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_VALUE);
        return;
    }
    if (modbus_range_is_valid(start, count) == 0U)
    {
        modbus_send_exception(frame[0],
                              frame[1],
                              MODBUS_EX_ILLEGAL_ADDRESS);
        return;
    }

    /* 完整校验地址范围后再写入，避免只写入一部分寄存器。 */
    for (index = 0U; index < count; index++)
    {
        s_modbus_holding_regs[start + index] =
            modbus_read_u16(&frame[7U + (index * 2U)]);
    }

    /* 10 响应只返回起始地址和成功写入的寄存器数量。 */
    if (frame[0] != MODBUS_BROADCAST_ADDR)
    {
        s_modbus_tx_buf[0] = frame[0];
        s_modbus_tx_buf[1] = MODBUS_FUNC_WRITE_MULTIPLE;
        modbus_write_u16(&s_modbus_tx_buf[2], start);
        modbus_write_u16(&s_modbus_tx_buf[4], count);
        modbus_send_frame(6U);
    }
}

/**
 * @brief Append CRC to the prepared response and start RS485 transmission.
 * @param[in] length Response length before the two CRC bytes are appended.
 */
static void modbus_send_frame(uint16_t length)
{
    uint16_t crc;

    if ((length + MODBUS_CRC_SIZE) > MODBUS_TX_BUF_SIZE)
    {
        return;
    }

    /* 在响应末尾按低字节、高字节顺序追加 CRC。 */
    crc = modbus_crc16(s_modbus_tx_buf, length);
    s_modbus_tx_buf[length] = (uint8_t)(crc & 0x00FFU);
    s_modbus_tx_buf[length + 1U] = (uint8_t)(crc >> 8U);
    /* RS485 采用非阻塞 DMA；只有成功启动发送才增加帧计数。 */
    if (rs485_send(s_modbus_tx_buf,
                   (uint16_t)(length + MODBUS_CRC_SIZE)) != 0U)
    {
        s_modbus_tx_frame_count++;
    }
}

/**
 * @brief Build and send a Modbus exception response.
 * @param[in] slave_addr Request destination; broadcasts produce no response.
 * @param[in] function Original request function code.
 * @param[in] exception Modbus exception code.
 */
static void modbus_send_exception(uint8_t slave_addr,
                                  uint8_t function,
                                  uint8_t exception)
{
    /* 广播写请求即使出错也不能返回异常帧。 */
    if (slave_addr == MODBUS_BROADCAST_ADDR)
    {
        return;
    }

    /* 异常响应把原功能码 bit7 置 1，第三字节为异常码。 */
    s_modbus_tx_buf[0] = slave_addr;
    s_modbus_tx_buf[1] = (uint8_t)(function | 0x80U);
    s_modbus_tx_buf[2] = exception;
    s_modbus_exception_count++;
    modbus_send_frame(3U);
}

/**
 * @brief Decode one big-endian Modbus 16-bit field.
 * @param[in] data Pointer to the field high byte.
 * @return Decoded host-order value.
 */
static uint16_t modbus_read_u16(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8U) | data[1]);
}

/**
 * @brief Encode one host-order value as a big-endian Modbus field.
 * @param[out] data Destination for two bytes.
 * @param[in] value Value to encode.
 */
static void modbus_write_u16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8U);
    data[1] = (uint8_t)(value & 0x00FFU);
}

/**
 * @brief Check that a register span fits in the local holding-register table.
 * @param[in] start First zero-based register address.
 * @param[in] count Number of consecutive registers.
 * @return 1 when the whole range is valid; otherwise 0.
 */
static uint8_t modbus_range_is_valid(uint16_t start, uint16_t count)
{
    /* 采用减法判断，避免 start+count 的 16 位加法溢出。 */
    if (start >= MODBUS_HOLDING_REG_COUNT)
    {
        return 0U;
    }
    return (count <= (MODBUS_HOLDING_REG_COUNT - start)) ? 1U : 0U;
}

/**
 * @brief Determine the complete request size from currently buffered bytes.
 * @param[in] frame Start of the receive accumulator.
 * @param[in] length Number of bytes currently available.
 * @return Expected full-frame length, or 0 when more header bytes are needed.
 */
static uint16_t modbus_expected_length(const uint8_t *frame,
                                       uint16_t length)
{
    uint16_t expected_length;

    if (length < 2U)
    {
        return 0U;
    }

    /* 03 和 06 请求固定为 8 字节。 */
    if ((frame[1] == MODBUS_FUNC_READ_HOLDING)
        || (frame[1] == MODBUS_FUNC_WRITE_SINGLE))
    {
        return MODBUS_REQUEST_FIXED_SIZE;
    }

    /* 10 的第 7 字节给出后续数据字节数，因此至少收到 7 字节才能判断。 */
    if (frame[1] == MODBUS_FUNC_WRITE_MULTIPLE)
    {
        if (length < 7U)
        {
            return 0U;
        }
        expected_length = (uint16_t)(9U + frame[6]);
        if (expected_length > MODBUS_RX_BUF_SIZE)
        {
            return MODBUS_RX_BUF_SIZE;
        }
        return expected_length;
    }

    /* 未支持功能码没有固定长度，收到一段 CRC 正确的数据就交给异常处理。 */
    if ((length >= MODBUS_MIN_FRAME_SIZE)
        && (modbus_check_crc(frame, length) != 0U))
    {
        return length;
    }
    return 0U;
}
