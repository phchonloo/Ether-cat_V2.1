#ifndef MODBUS_H
#define MODBUS_H

#include "N32G45XSYS.h"

#define MODBUS_DEFAULT_SLAVE_ADDR  (1U)
#define MODBUS_DEFAULT_BAUDRATE    (115200U)
#define MODBUS_RX_BUF_SIZE         (256U)
#define MODBUS_TX_BUF_SIZE         (256U)
#define MODBUS_MIN_FRAME_SIZE      (4U)
#define MODBUS_CRC_SIZE            (2U)
#define MODBUS_BROADCAST_ADDR      (0U)
#define MODBUS_MAX_SLAVE_ADDR      (247U)
#define MODBUS_MAX_READ_REG_COUNT  (125U)
#define MODBUS_MAX_WRITE_REG_COUNT (123U)
#define MODBUS_HOLDING_REG_COUNT   (128U)
#define MODBUS_REQUEST_FIXED_SIZE  (8U)
#define MODBUS_WRITE_MULTIPLE_MIN_SIZE (9U)

#define MODBUS_FUNC_READ_HOLDING   (0x03U)
#define MODBUS_FUNC_WRITE_SINGLE   (0x06U)
#define MODBUS_FUNC_WRITE_MULTIPLE (0x10U)

#define MODBUS_EX_ILLEGAL_FUNCTION (0x01U)
#define MODBUS_EX_ILLEGAL_ADDRESS  (0x02U)
#define MODBUS_EX_ILLEGAL_VALUE    (0x03U)

/**
 * @brief Initialize the Modbus RTU slave and its RS485 transport.
 * @param[in] slave_addr Slave address in the range 1..247; an invalid value
 *                       selects MODBUS_DEFAULT_SLAVE_ADDR.
 * @param[in] baudrate UART bit rate; zero selects MODBUS_DEFAULT_BAUDRATE.
 */
void modbus_init(uint8_t slave_addr, uint32_t baudrate);

/**
 * @brief Process received Modbus frames and send pending replies.
 * @note Call repeatedly from the main loop. Keep this function outside the
 *       EtherCAT interrupt path so protocol processing cannot delay ESC IRQs.
 */
void modbus_process(void);

/**
 * @brief Read one local holding register.
 * @param[in] address Zero-based register address.
 * @param[out] value Receives the 16-bit register value.
 * @return 1 on success; 0 for a null pointer or an out-of-range address.
 */
uint8_t modbus_get_holding(uint16_t address, uint16_t *value);

/**
 * @brief Write one local holding register.
 * @param[in] address Zero-based register address.
 * @param[in] value New 16-bit register value.
 * @return 1 on success; 0 when the address is out of range.
 */
uint8_t modbus_set_holding(uint16_t address, uint16_t value);

/**
 * @brief Calculate the Modbus RTU CRC-16 value.
 * @param[in] data Input byte sequence.
 * @param[in] length Number of bytes included in the calculation.
 * @return CRC value in host byte order; RTU transmits its low byte first.
 */
uint16_t modbus_crc16(const uint8_t *data, uint16_t length);

#endif
