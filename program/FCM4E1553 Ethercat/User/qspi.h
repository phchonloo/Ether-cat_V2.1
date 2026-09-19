#ifndef USER_QSPI_H
#define USER_QSPI_H

#include "N32G45XSYS.h"

#define QSPI_AFIO_PORT_SEL                      QSPI_NSS_PORTA_SEL
#define CLOCK_DIVIDER                           10
#define CTRL1_NDF_CNT                           1024

/**
 * @brief Initialize the QSPI peripheral used as the EtherCAT ESC PDI.
 * @param[in] qspi_format_sel Reserved for interface compatibility.
 * @param[in] data_dir Reserved for interface compatibility.
 * @param[in] count Reserved for interface compatibility.
 * @note Current hardware settings are fixed internally; the three parameters
 *       are intentionally ignored.
 */
void qspi_init(QSPI_FORMAT_SEL qspi_format_sel,
               QSPI_DATA_DIR data_dir,
               uint16_t count);

/**
 * @brief Read one 32-bit value from the ESC PDI address space.
 * @param[in] Address ESC byte address.
 * @return Little-endian 32-bit value assembled from four QSPI bytes.
 */
uint32_t qspi_read_word(uint16_t address);

/**
 * @brief Read a byte block from the ESC PDI address space.
 * @param[out] ReadBuffer Destination byte buffer.
 * @param[in] Address ESC start address.
 * @param[in] Count Number of bytes to read; must be greater than zero.
 */
void qspi_read(uint8_t *read_buffer,
               uint16_t address,
               uint8_t count);

/**
 * @brief Write a byte block to the ESC PDI address space.
 * @param[in] WriteBuffer Source byte buffer.
 * @param[in] Address ESC start address.
 * @param[in] Count Number of bytes to write.
 */
void qspi_write(uint8_t *write_buffer,
                uint16_t address,
                uint16_t count);

typedef union
{
    uint32_t Val;
    uint8_t v[4];
    uint16_t w[2];
    struct
    {
        uint8_t LB;
        uint8_t HB;
        uint8_t UB;
        uint8_t MB;
    } byte;
} UINT32_VAL;

typedef union
{
    uint16_t Val;
    struct
    {
        uint8_t LB;
        uint8_t HB;
    } byte;
} UINT16_VAL;

typedef union
{
    uint16_t Len_byte;
    struct
    {
        uint8_t LB;
        uint8_t HB;
    } byte;
} UINT16_LEN;

#endif
