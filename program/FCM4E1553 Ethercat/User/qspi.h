#ifndef USER_QSPI_H
#define USER_QSPI_H

#include "N32G45XSYS.h"

#define QSPI_AFIO_PORT_SEL                      QSPI_NSS_PORTA_SEL
#define CLOCK_DIVIDER                           10
#define CTRL1_NDF_CNT                           1024

/* Debugger-visible, retained until MCU reset. firstError is committed last.
 * These are MCU/PDI errors, not the EtherCAT master's working counter. */
typedef struct
{
    uint32_t transferCount;
    uint32_t timeoutCount;
    uint32_t readFailureCount;
    uint32_t writeFailureCount;
    uint32_t errorEpoch;
    uint32_t successSinceError;
    uint32_t firstStatus;
    uint32_t firstRxCount;
    uint32_t firstCycle;
    uint16_t firstAddress;
    uint16_t firstLength;
    uint16_t firstException;
    uint8_t firstError;
    uint8_t lastError;
    uint8_t faultLatched;
} TEscPdiDiagnostics;

enum
{
    ESC_PDI_ERROR_ARGUMENT = 1,
    ESC_PDI_ERROR_IDLE_TIMEOUT = 2,
    ESC_PDI_ERROR_READ_TIMEOUT = 3,
    ESC_PDI_ERROR_RX_SHORT = 4,
    ESC_PDI_ERROR_WRITE_TIMEOUT = 5,
    ESC_PDI_ERROR_SM_ACK = 6
};

extern volatile TEscPdiDiagnostics gEscPdiDiag;
void qspi_pdi_record_error(uint8_t reason, uint16_t address,
                           uint16_t count, uint8_t is_write);

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
/* Return 1 only for a complete transfer; destination is unchanged on failure.
 * The port serializes access and splits blocks into at most four bytes. */
uint8_t qspi_read(uint8_t *read_buffer,
               uint16_t address,
               uint8_t count);

/**
 * @brief Write a byte block to the ESC PDI address space.
 * @param[in] WriteBuffer Source byte buffer.
 * @param[in] Address ESC start address.
 * @param[in] Count Number of bytes to write.
 */
uint8_t qspi_write(uint8_t *write_buffer,
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
