#include "mcuinit.h"

#define LEGACY_I2C_EVENT_TIMEOUT (100000U)

static uint8_t s_legacy_i2c_error;
static uint8_t s_legacy_i2c_expect_address;

static uint8_t legacy_i2c_wait_event(uint32_t event)
{
    uint32_t timeout;

    timeout = LEGACY_I2C_EVENT_TIMEOUT;
    while (timeout > 0U)
    {
        if (I2C_CheckEvent(BSP_24C02_I2C, event) == SUCCESS)
        {
            return 0U;
        }

        if (I2C_GetFlag(BSP_24C02_I2C, I2C_FLAG_ACKFAIL) != RESET)
        {
            I2C_ClrFlag(BSP_24C02_I2C, I2C_FLAG_ACKFAIL);
            return 1U;
        }
        timeout--;
    }

    return 1U;
}

void delay_i2c(uint32_t nCount)
{
    volatile uint32_t delay;

    for (delay = 0U; delay < (nCount * 4U); delay++)
    {
        __NOP();
    }
}

void i2c_init(void)
{
    eeprom_init();
    s_legacy_i2c_error = 0U;
    s_legacy_i2c_expect_address = 0U;
}

void IIC_Start(void)
{
    I2C_ConfigAck(BSP_24C02_I2C, ENABLE);
    I2C_GenerateStart(BSP_24C02_I2C, ENABLE);
    s_legacy_i2c_error = legacy_i2c_wait_event(
        I2C_EVT_MASTER_MODE_FLAG);
    s_legacy_i2c_expect_address = 1U;
}

void IIC_Stop(void)
{
    I2C_GenerateStop(BSP_24C02_I2C, ENABLE);
    I2C_ConfigAck(BSP_24C02_I2C, ENABLE);
    s_legacy_i2c_expect_address = 0U;
}

uint8_t IIC_Wait_Ack(void)
{
    return s_legacy_i2c_error;
}

void IIC_Ack(void)
{
    I2C_ConfigAck(BSP_24C02_I2C, ENABLE);
}

void IIC_NAck(void)
{
    I2C_ConfigAck(BSP_24C02_I2C, DISABLE);
}

void IIC_Send_Byte(uint8_t txd)
{
    uint8_t address;
    uint16_t direction;

    if (s_legacy_i2c_error != 0U)
    {
        return;
    }

    if (s_legacy_i2c_expect_address != 0U)
    {
        direction = ((txd & 0x01U) != 0U)
                        ? I2C_DIRECTION_RECV
                        : I2C_DIRECTION_SEND;

        if ((txd == (BSP_24C02_DEVICE_ADDR_7BIT << 1U))
            || (txd == ((BSP_24C02_DEVICE_ADDR_7BIT << 1U) | 1U))
            || (txd == BSP_24C02_DEVICE_ADDR_7BIT)
            || (txd == (BSP_24C02_DEVICE_ADDR_7BIT | 1U)))
        {
            address = BSP_24C02_DEVICE_ADDR_7BIT;
        }
        else
        {
            address = (txd > 0x7FU) ? (uint8_t)(txd >> 1U) : txd;
        }

        I2C_SendAddr7bit(BSP_24C02_I2C, address, direction);
        s_legacy_i2c_error = legacy_i2c_wait_event(
            (direction == I2C_DIRECTION_RECV)
                ? I2C_EVT_MASTER_RXMODE_FLAG
                : I2C_EVT_MASTER_TXMODE_FLAG);
        s_legacy_i2c_expect_address = 0U;
    }
    else
    {
        I2C_SendData(BSP_24C02_I2C, txd);
        s_legacy_i2c_error = legacy_i2c_wait_event(
            I2C_EVT_MASTER_DATA_SENDED);
    }
}

uint8_t IIC_Read_Byte(unsigned char ack)
{
    if (ack != 0U)
    {
        IIC_Ack();
    }
    else
    {
        IIC_NAck();
    }

    s_legacy_i2c_error = legacy_i2c_wait_event(
        I2C_EVT_MASTER_DATA_RECVD_FLAG);
    if (s_legacy_i2c_error != 0U)
    {
        return 0U;
    }

    return I2C_RecvData(BSP_24C02_I2C);
}

int32_t i2cWriteBuffer(uint8_t addr,
                       uint8_t reg,
                       uint8_t len,
                       uint8_t *data)
{
    if ((addr != (BSP_24C02_DEVICE_ADDR_7BIT << 1U))
        && (addr != BSP_24C02_DEVICE_ADDR_7BIT))
    {
        return -1;
    }
    return (eeprom_write(reg, data, len) == BSP_24C02_OK) ? 0 : -1;
}

int32_t i2cRead(uint8_t addr,
                uint8_t reg,
                uint8_t len,
                uint8_t *buf)
{
    if ((addr != (BSP_24C02_DEVICE_ADDR_7BIT << 1U))
        && (addr != BSP_24C02_DEVICE_ADDR_7BIT))
    {
        return -1;
    }
    return (eeprom_read(reg, buf, len) == BSP_24C02_OK) ? 0 : -1;
}
