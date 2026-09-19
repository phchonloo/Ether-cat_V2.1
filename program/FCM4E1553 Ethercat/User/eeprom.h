#ifndef USER_EEPROM_H
#define USER_EEPROM_H

#include "N32G45XSYS.h"

/* 24C02连接I2C1默认引脚：PB6=SCL，PB7=SDA。 */
#define BSP_24C02_I2C                    I2C1
#define BSP_24C02_I2C_CLK                RCC_APB1_PERIPH_I2C1
#define BSP_24C02_GPIO_CLK               (RCC_APB2_PERIPH_GPIOB \
                                          | RCC_APB2_PERIPH_AFIO)
#define BSP_24C02_GPIO_PORT              GPIOB
#define BSP_24C02_SCL_PIN                GPIO_PIN_6
#define BSP_24C02_SDA_PIN                GPIO_PIN_7
#define BSP_24C02_GPIO_REMAP             GPIO_RMP_I2C1

#define BSP_24C02_I2C_SPEED_HZ           (100000U)
#define BSP_24C02_DEVICE_ADDR_7BIT       (0x50U)
#define BSP_24C02_CAPACITY_BYTES         (256U)
#define BSP_24C02_PAGE_SIZE_BYTES        (8U)
#define BSP_24C02_EVENT_TIMEOUT          (100000U)
#define BSP_24C02_WRITE_POLL_LIMIT       (1000U)

typedef enum
{
    BSP_24C02_OK = 0,
    BSP_24C02_ERROR_INVALID_ARGUMENT,
    BSP_24C02_ERROR_NOT_INITIALIZED,
    BSP_24C02_ERROR_BUS_BUSY,
    BSP_24C02_ERROR_TIMEOUT,
    BSP_24C02_ERROR_NACK,
    BSP_24C02_ERROR_BUS,
    BSP_24C02_ERROR_VERIFY,
    BSP_24C02_ERROR_RESTORE
} eeprom_status_t;

/**
 * @brief 初始化I2C1及PB6/PB7上的24C02。
 */
void eeprom_init(void);

/**
 * @brief 检测24C02是否应答。
 * @return BSP_24C02_OK表示器件在线。
 */
eeprom_status_t eeprom_is_ready(void);

/**
 * @brief 从24C02连续读取数据。
 * @param[in] address 起始地址。
 * @param[out] data 接收缓冲区。
 * @param[in] length 读取长度。
 * @return 24C02操作状态。
 */
eeprom_status_t eeprom_read(uint8_t address,
                            uint8_t *data,
                            uint16_t length);

/**
 * @brief 向24C02连续写入数据，并自动处理8字节页边界。
 * @param[in] address 起始地址。
 * @param[in] data 待写入数据。
 * @param[in] length 写入长度。
 * @return 24C02操作状态。
 */
eeprom_status_t eeprom_write(uint8_t address,
                             const uint8_t *data,
                             uint16_t length);

/**
 * @brief 备份、写入、校验并恢复24C02最后8个字节。
 * @return BSP_24C02_OK表示自检和数据恢复成功。
 */
eeprom_status_t eeprom_test(void);

#endif
