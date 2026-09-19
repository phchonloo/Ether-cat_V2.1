#include "eeprom.h"

#define BSP_24C02_DEVICE_ADDR_8BIT \
    ((uint8_t)(BSP_24C02_DEVICE_ADDR_7BIT << 1U))
#define BSP_24C02_I2C_ERROR_MASK \
    (I2C_STS1_BUSERR | I2C_STS1_ARLOST | I2C_STS1_OVERRUN)

static volatile eeprom_status_t s_eeprom_status =
    BSP_24C02_ERROR_NOT_INITIALIZED;
static volatile uint32_t s_eeprom_error_count;

static uint8_t s_eeprom_initialized = 0U;

static void eeprom_i2c_configure(void);
static void eeprom_abort_transfer(uint8_t reset_peripheral);
static eeprom_status_t eeprom_wait_bus_idle(void);
static eeprom_status_t eeprom_wait_event(uint32_t event);
static eeprom_status_t eeprom_start_address(uint8_t direction);
static eeprom_status_t eeprom_probe_once(void);
static eeprom_status_t eeprom_wait_write_complete(void);
static eeprom_status_t eeprom_write_page(uint8_t address,
                                         const uint8_t *data,
                                         uint16_t length);
static eeprom_status_t eeprom_finish(eeprom_status_t status);

/**
 * @brief 初始化I2C1和24C02使用的PB6、PB7引脚。
 */
void eeprom_init(void)
{
    GPIO_InitType gpio_init;

    RCC_EnableAPB2PeriphClk(BSP_24C02_GPIO_CLK, ENABLE);
    RCC_EnableAPB1PeriphClk(BSP_24C02_I2C_CLK, ENABLE);
    GPIO_ConfigPinRemap(BSP_24C02_GPIO_REMAP, DISABLE);

    gpio_init.Pin = BSP_24C02_SCL_PIN | BSP_24C02_SDA_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_OD;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitPeripheral(BSP_24C02_GPIO_PORT, &gpio_init);

    eeprom_i2c_configure();
    s_eeprom_initialized = 1U;
    s_eeprom_status = BSP_24C02_OK;
}

/**
 * @brief 检测24C02是否应答。
 */
eeprom_status_t eeprom_is_ready(void)
{
    eeprom_status_t status;

    if (s_eeprom_initialized == 0U)
    {
        return eeprom_finish(BSP_24C02_ERROR_NOT_INITIALIZED);
    }

    status = eeprom_probe_once();
    if ((status != BSP_24C02_OK) &&
        (status != BSP_24C02_ERROR_NACK))
    {
        eeprom_abort_transfer(1U);
    }
    return eeprom_finish(status);
}

/**
 * @brief 从指定地址连续读取24C02数据。
 */
eeprom_status_t eeprom_read(uint8_t address,
                            uint8_t *data,
                            uint16_t length)
{
    eeprom_status_t status;
    uint16_t remaining;

    if (s_eeprom_initialized == 0U)
    {
        return eeprom_finish(BSP_24C02_ERROR_NOT_INITIALIZED);
    }
    if ((length != 0U) && (data == NULL))
    {
        return eeprom_finish(BSP_24C02_ERROR_INVALID_ARGUMENT);
    }
    if (((uint16_t)address + length) > BSP_24C02_CAPACITY_BYTES)
    {
        return eeprom_finish(BSP_24C02_ERROR_INVALID_ARGUMENT);
    }
    if (length == 0U)
    {
        return eeprom_finish(BSP_24C02_OK);
    }

    status = eeprom_wait_bus_idle();
    if (status != BSP_24C02_OK)
    {
        eeprom_abort_transfer(1U);
        return eeprom_finish(status);
    }

    I2C_ConfigAck(BSP_24C02_I2C, ENABLE);
    I2C_ConfigNackLocation(BSP_24C02_I2C, I2C_NACK_POS_CURRENT);

    /* 先写入内部地址，再用重复起始信号切换到读方向。 */
    status = eeprom_start_address(I2C_DIRECTION_SEND);
    if (status == BSP_24C02_OK)
    {
        I2C_SendData(BSP_24C02_I2C, address);
        status = eeprom_wait_event(I2C_EVT_MASTER_DATA_SENDED);
    }
    if (status == BSP_24C02_OK)
    {
        status = eeprom_start_address(I2C_DIRECTION_RECV);
    }

    remaining = length;
    while ((status == BSP_24C02_OK) && (remaining != 0U))
    {
        if (remaining == 1U)
        {
            I2C_ConfigAck(BSP_24C02_I2C, DISABLE);
            I2C_GenerateStop(BSP_24C02_I2C, ENABLE);
        }

        status = eeprom_wait_event(I2C_EVT_MASTER_DATA_RECVD_FLAG);
        if (status == BSP_24C02_OK)
        {
            *data = I2C_RecvData(BSP_24C02_I2C);
            data++;
            remaining--;
        }
    }

    I2C_ConfigAck(BSP_24C02_I2C, ENABLE);
    if (status != BSP_24C02_OK)
    {
        eeprom_abort_transfer(
            status == BSP_24C02_ERROR_NACK ? 0U : 1U);
    }
    return eeprom_finish(status);
}

/**
 * @brief 向指定地址连续写入24C02数据。
 */
eeprom_status_t eeprom_write(uint8_t address,
                             const uint8_t *data,
                             uint16_t length)
{
    eeprom_status_t status;
    uint16_t page_space;
    uint16_t chunk;

    if (s_eeprom_initialized == 0U)
    {
        return eeprom_finish(BSP_24C02_ERROR_NOT_INITIALIZED);
    }
    if ((length != 0U) && (data == NULL))
    {
        return eeprom_finish(BSP_24C02_ERROR_INVALID_ARGUMENT);
    }
    if (((uint16_t)address + length) > BSP_24C02_CAPACITY_BYTES)
    {
        return eeprom_finish(BSP_24C02_ERROR_INVALID_ARGUMENT);
    }

    /* 每次页写都限制在同一个8字节页内。 */
    status = BSP_24C02_OK;
    while ((status == BSP_24C02_OK) && (length != 0U))
    {
        page_space = BSP_24C02_PAGE_SIZE_BYTES
                   - ((uint16_t)address % BSP_24C02_PAGE_SIZE_BYTES);
        chunk = (length < page_space) ? length : page_space;
        status = eeprom_write_page(address, data, chunk);
        if (status == BSP_24C02_OK)
        {
            address = (uint8_t)((uint16_t)address + chunk);
            data += chunk;
            length -= chunk;
        }
    }
    return eeprom_finish(status);
}

/**
 * @brief 测试最后8个字节，并在测试结束后恢复原数据。
 */
eeprom_status_t eeprom_test(void)
{
    static const uint8_t test_data[8] = {
        0x55U, 0xAAU, 0x00U, 0xFFU,
        0x12U, 0x34U, 0x56U, 0x78U
    };
    uint8_t backup[8];
    uint8_t readback[8];
    eeprom_status_t test_status;
    eeprom_status_t restore_status;
    uint16_t index;

    test_status = eeprom_is_ready();
    if (test_status != BSP_24C02_OK)
    {
        return test_status;
    }

    test_status = eeprom_read(0xF8U, backup, 8U);
    if (test_status != BSP_24C02_OK)
    {
        return test_status;
    }

    test_status = eeprom_write(0xF8U, test_data, 8U);
    if (test_status == BSP_24C02_OK)
    {
        test_status = eeprom_read(0xF8U, readback, 8U);
    }
    if (test_status == BSP_24C02_OK)
    {
        for (index = 0U; index < 8U; index++)
        {
            if (readback[index] != test_data[index])
            {
                test_status = BSP_24C02_ERROR_VERIFY;
                break;
            }
        }
    }

    restore_status = eeprom_write(0xF8U, backup, 8U);
    if (restore_status == BSP_24C02_OK)
    {
        restore_status = eeprom_read(0xF8U, readback, 8U);
    }
    if (restore_status == BSP_24C02_OK)
    {
        for (index = 0U; index < 8U; index++)
        {
            if (readback[index] != backup[index])
            {
                restore_status = BSP_24C02_ERROR_RESTORE;
                break;
            }
        }
    }
    if (restore_status != BSP_24C02_OK)
    {
        return eeprom_finish(BSP_24C02_ERROR_RESTORE);
    }
    return eeprom_finish(test_status);
}

/**
 * @brief 重新配置I2C1为100 kHz、7位地址模式。
 */
static void eeprom_i2c_configure(void)
{
    I2C_InitType i2c_init;

    I2C_DeInit(BSP_24C02_I2C);
    I2C_InitStruct(&i2c_init);
    i2c_init.ClkSpeed = BSP_24C02_I2C_SPEED_HZ;
    i2c_init.BusMode = I2C_BUSMODE_I2C;
    i2c_init.FmDutyCycle = I2C_FMDUTYCYCLE_2;
    i2c_init.OwnAddr1 = 0U;
    i2c_init.AckEnable = I2C_ACKEN;
    i2c_init.AddrMode = I2C_ADDR_MODE_7BIT;
    I2C_Init(BSP_24C02_I2C, &i2c_init);
    I2C_Enable(BSP_24C02_I2C, ENABLE);
    I2C_ConfigAck(BSP_24C02_I2C, ENABLE);
}

/**
 * @brief 终止失败的传输，必要时软件复位并重配I2C1。
 */
static void eeprom_abort_transfer(uint8_t reset_peripheral)
{
    I2C_GenerateStop(BSP_24C02_I2C, ENABLE);
    I2C_ConfigAck(BSP_24C02_I2C, ENABLE);
    I2C_ClrFlag(BSP_24C02_I2C,
                I2C_FLAG_ACKFAIL | I2C_FLAG_ARLOST
                | I2C_FLAG_BUSERR | I2C_FLAG_OVERRUN);

    if (reset_peripheral != 0U)
    {
        I2C_EnableSoftwareReset(BSP_24C02_I2C, ENABLE);
        I2C_EnableSoftwareReset(BSP_24C02_I2C, DISABLE);
        eeprom_i2c_configure();
    }
}

/**
 * @brief 等待I2C总线空闲。
 */
static eeprom_status_t eeprom_wait_bus_idle(void)
{
    uint32_t timeout;

    timeout = BSP_24C02_EVENT_TIMEOUT;
    while (I2C_GetFlag(BSP_24C02_I2C, I2C_FLAG_BUSY) != RESET)
    {
        if (timeout == 0U)
        {
            return BSP_24C02_ERROR_BUS_BUSY;
        }
        timeout--;
    }
    return BSP_24C02_OK;
}

/**
 * @brief 等待I2C事件，同时检测NACK和总线错误。
 */
static eeprom_status_t eeprom_wait_event(uint32_t event)
{
    uint32_t timeout;
    uint16_t status1;

    timeout = BSP_24C02_EVENT_TIMEOUT;
    while (timeout != 0U)
    {
        status1 = BSP_24C02_I2C->STS1;
        if ((status1 & I2C_STS1_ACKFAIL) != 0U)
        {
            I2C_ClrFlag(BSP_24C02_I2C, I2C_FLAG_ACKFAIL);
            return BSP_24C02_ERROR_NACK;
        }
        if ((status1 & BSP_24C02_I2C_ERROR_MASK) != 0U)
        {
            return BSP_24C02_ERROR_BUS;
        }
        if (I2C_CheckEvent(BSP_24C02_I2C, event) == SUCCESS)
        {
            return BSP_24C02_OK;
        }
        timeout--;
    }
    return BSP_24C02_ERROR_TIMEOUT;
}

/**
 * @brief 产生START并发送24C02器件地址。
 */
static eeprom_status_t eeprom_start_address(uint8_t direction)
{
    eeprom_status_t status;

    I2C_GenerateStart(BSP_24C02_I2C, ENABLE);
    status = eeprom_wait_event(I2C_EVT_MASTER_MODE_FLAG);
    if (status != BSP_24C02_OK)
    {
        return status;
    }

    I2C_SendAddr7bit(BSP_24C02_I2C,
                     BSP_24C02_DEVICE_ADDR_8BIT,
                     direction);
    if (direction == I2C_DIRECTION_RECV)
    {
        return eeprom_wait_event(I2C_EVT_MASTER_RXMODE_FLAG);
    }
    return eeprom_wait_event(I2C_EVT_MASTER_TXMODE_FLAG);
}

/**
 * @brief 对24C02执行一次ACK探测。
 */
static eeprom_status_t eeprom_probe_once(void)
{
    eeprom_status_t status;

    status = eeprom_wait_bus_idle();
    if (status != BSP_24C02_OK)
    {
        return status;
    }

    status = eeprom_start_address(I2C_DIRECTION_SEND);
    I2C_GenerateStop(BSP_24C02_I2C, ENABLE);
    if (status == BSP_24C02_ERROR_NACK)
    {
        I2C_ClrFlag(BSP_24C02_I2C, I2C_FLAG_ACKFAIL);
    }
    return status;
}

/**
 * @brief 通过ACK轮询等待24C02内部页写完成。
 */
static eeprom_status_t eeprom_wait_write_complete(void)
{
    eeprom_status_t status;
    uint32_t attempt;

    for (attempt = 0U;
         attempt < BSP_24C02_WRITE_POLL_LIMIT;
         attempt++)
    {
        status = eeprom_probe_once();
        if (status == BSP_24C02_OK)
        {
            return BSP_24C02_OK;
        }
        if (status != BSP_24C02_ERROR_NACK)
        {
            eeprom_abort_transfer(1U);
            return status;
        }
    }

    eeprom_abort_transfer(1U);
    return BSP_24C02_ERROR_TIMEOUT;
}

/**
 * @brief 写入一段不跨越页边界的数据。
 */
static eeprom_status_t eeprom_write_page(uint8_t address,
                                         const uint8_t *data,
                                         uint16_t length)
{
    eeprom_status_t status;

    status = eeprom_wait_bus_idle();
    if (status == BSP_24C02_OK)
    {
        status = eeprom_start_address(I2C_DIRECTION_SEND);
    }
    if (status == BSP_24C02_OK)
    {
        I2C_SendData(BSP_24C02_I2C, address);
        status = eeprom_wait_event(I2C_EVT_MASTER_DATA_SENDED);
    }

    while ((status == BSP_24C02_OK) && (length != 0U))
    {
        I2C_SendData(BSP_24C02_I2C, *data);
        data++;
        length--;
        status = eeprom_wait_event(I2C_EVT_MASTER_DATA_SENDED);
    }

    I2C_GenerateStop(BSP_24C02_I2C, ENABLE);
    if (status != BSP_24C02_OK)
    {
        eeprom_abort_transfer(
            status == BSP_24C02_ERROR_NACK ? 0U : 1U);
        return status;
    }
    return eeprom_wait_write_complete();
}

/**
 * @brief 记录最近一次操作状态和累计错误次数。
 */
static eeprom_status_t eeprom_finish(eeprom_status_t status)
{
    s_eeprom_status = status;
    if (status != BSP_24C02_OK)
    {
        s_eeprom_error_count++;
    }
    return status;
}
