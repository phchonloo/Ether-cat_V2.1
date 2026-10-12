#include "qspi.h"

#define QSPI_PDI_BUSY_TIMEOUT_LOOPS (1000U)
#define QSPI_PDI_BUSY_TIMEOUT_US (100U)

static volatile uint32_t s_qspi_timeout_count;
volatile TEscPdiDiagnostics gEscPdiDiag = {0};

static void qspi_delay(uint32_t count);
static uint32_t qspi_wait(void);

/**
 * @brief Latch a PDI failure, preserve evidence and abort this transaction.
 */
void qspi_pdi_record_error(uint8_t reason, uint16_t address,
                           uint16_t count, uint8_t is_write)
{
    /* Called with PDI access serialized. Never print, allocate or retry here. */
    gEscPdiDiag.faultLatched = 1U;
    gEscPdiDiag.successSinceError = 0U;
    gEscPdiDiag.errorEpoch++;
    if (gEscPdiDiag.errorEpoch == 0U)
        gEscPdiDiag.errorEpoch = 1U;
    gEscPdiDiag.lastError = reason;
    if (is_write != 0U)
    {
        if (gEscPdiDiag.writeFailureCount != UINT32_MAX)
            gEscPdiDiag.writeFailureCount++;
    }
    else if (gEscPdiDiag.readFailureCount != UINT32_MAX)
        gEscPdiDiag.readFailureCount++;
    if ((reason == ESC_PDI_ERROR_IDLE_TIMEOUT) ||
        (reason == ESC_PDI_ERROR_READ_TIMEOUT) ||
        (reason == ESC_PDI_ERROR_WRITE_TIMEOUT))
    {
        if (s_qspi_timeout_count != UINT32_MAX)
            s_qspi_timeout_count++;
        gEscPdiDiag.timeoutCount = s_qspi_timeout_count;
    }
    if (gEscPdiDiag.firstError == 0U)
    {
        gEscPdiDiag.firstAddress = address;
        gEscPdiDiag.firstLength = count;
        gEscPdiDiag.firstException = (uint16_t)__get_IPSR();
        gEscPdiDiag.firstStatus = QSPI->STS;
        gEscPdiDiag.firstRxCount = QSPI->RXFN;
        gEscPdiDiag.firstCycle = DWT->CYCCNT;
        __DMB();
        gEscPdiDiag.firstError = reason;
    }
    /* Abort this MCU transaction. Never reset the ESC or replay a partial
     * write: the next access starts a new CS-framed transaction. */
    QSPI_Cmd(DISABLE);
    QSPI_DeInit();
}

static uint8_t qspi_wait_checked(uint16_t address, uint16_t count,
                                 uint8_t reason, uint8_t is_write)
{
    if (qspi_wait() != 0U)
        return 1U;
    qspi_pdi_record_error(reason, address, count, is_write);
    return 0U;
}

static void qspi_record_success(void)
{
    if (gEscPdiDiag.transferCount != UINT32_MAX)
        gEscPdiDiag.transferCount++;
    if (gEscPdiDiag.successSinceError != UINT32_MAX)
        gEscPdiDiag.successSinceError++;
}

void qspi_init(QSPI_FORMAT_SEL qspi_format_sel,
               QSPI_DATA_DIR data_dir,
               uint16_t count)
{
    QSPI_InitType qspi_init = {0};

    (void)qspi_format_sel;
    (void)data_dir;
    (void)count;

    QSPI_DeInit();
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_QSPI,
                           ENABLE);

    qspi_init.SPI_FRF =
        QSPI_CTRL0_SPI_FRF_STANDARD_FORMAT;
    qspi_init.TMOD = QSPI_CTRL0_TMOD_EEPROM_READ;
    qspi_init.SCPOL = QSPI_CTRL0_SCPOL_LOW;
    qspi_init.SCPH = QSPI_CTRL0_SCPH_FIRST_EDGE;
    qspi_init.DFS = QSPI_CTRL0_DFS_0;
    qspi_init.CLK_DIV = 20U;
    qspi_init.TXFT = QSPI_TXFT_TEI_0;
    qspi_init.RXFT = QSPI_RXFT_TFI_0;
    qspi_init.NDF = 3U;

    QspiInitConfig(&qspi_init);
    QSPI_Cmd(ENABLE);
}

uint8_t qspi_read(uint8_t *read_buffer,
               uint16_t address,
               uint8_t count)
{
    QSPI_InitType qspi_init;
    uint32_t wait_cycles;
    uint16_t requested_address = address;
    uint8_t requested_count = count;

    if ((read_buffer == 0) || (count == 0U) || (count > 4U))
    {
        qspi_pdi_record_error(ESC_PDI_ERROR_ARGUMENT, address, count, 0U);
        return 0U;
    }

    wait_cycles = (uint32_t)(address & 0x0003U) *
                  0x1000U;
    if (qspi_wait_checked(address, count, ESC_PDI_ERROR_IDLE_TIMEOUT, 0U) == 0U)
        return 0U;

    QSPI_DeInit();
    memset(&qspi_init, 0, sizeof(qspi_init));

    qspi_init.SPI_FRF = QSPI_CTRL0_SPI_FRF_QUAD_FORMAT;
    qspi_init.TMOD = QSPI_CTRL0_TMOD_RX_ONLY;
    qspi_init.SCPOL = QSPI_CTRL0_SCPOL_LOW;
    qspi_init.SCPH = QSPI_CTRL0_SCPH_FIRST_EDGE;
    qspi_init.ENHANCED_WAIT_CYCLES =
        QSPI_ENH_CTRL0_WAIT_6CYCLES + wait_cycles;
    qspi_init.CLK_DIV = 30U;
    qspi_init.TXFT = QSPI_TXFT_TEI_0;
    qspi_init.RXFT = QSPI_RXFT_TFI_0;
    qspi_init.NDF = (uint32_t)(count - 1U);
    qspi_init.ENHANCED_ADDR_LEN =
        QSPI_ENH_CTRL0_ADDR_LEN_24_BIT;
    qspi_init.ENHANCED_INST_L =
        QSPI_ENH_CTRL0_INST_L_8_LINE;
    qspi_init.ENHANCED_TRANS_TYPE =
        QSPI_ENH_CTRL0_TRANS_TYPE_ALL_BY_FRF;

    QspiInitConfig(&qspi_init);
    QSPI_Cmd(ENABLE);

    QspiSendWord(0x0BU);
    if (address < 0x3000U)
    {
        address |= 0x4000U;
    }

    QspiSendWord(((uint32_t)address << 8U) | count);
    QspiSendWord(0U);

    if (qspi_wait_checked(requested_address, requested_count,
                          ESC_PDI_ERROR_READ_TIMEOUT, 0U) == 0U)
        return 0U;
    /* Busy=0 alone does not prove the RX FIFO contains the requested data. */
    if (QspiReadRxFifoNum() < requested_count)
    {
        qspi_pdi_record_error(ESC_PDI_ERROR_RX_SHORT,
                              requested_address, requested_count, 0U);
        return 0U;
    }
    while (count > 0U)
    {
        *read_buffer = (uint8_t)QspiReadWord();
        read_buffer++;
        count--;
    }
    qspi_record_success();
    return 1U;
}

uint8_t qspi_write(uint8_t *write_buffer,
                uint16_t address,
                uint16_t count)
{
    QSPI_InitType qspi_init = {0};
    uint16_t requested_address = address;
    uint16_t requested_count = count;

    if ((write_buffer == 0) || (count == 0U) || (count > 4U))
    {
        qspi_pdi_record_error(ESC_PDI_ERROR_ARGUMENT, address, count, 1U);
        return 0U;
    }
    if (qspi_wait_checked(address, count, ESC_PDI_ERROR_IDLE_TIMEOUT, 1U) == 0U)
        return 0U;

    QSPI_DeInit();
    RCC_EnableAHBPeriphClk(RCC_AHB_PERIPH_QSPI,
                           ENABLE);
    GPIO_ConfigPinRemap(GPIO_RMP_QSPI_XIP_EN,
                        DISABLE);

    qspi_init.SPI_FRF = QSPI_CTRL0_SPI_FRF_QUAD_FORMAT;
    qspi_init.TMOD = QSPI_CTRL0_TMOD_TX_ONLY;
    qspi_init.NDF = 0U;
    qspi_init.ENHANCED_WAIT_CYCLES = 0U;
    qspi_init.CFS = QSPI_CTRL0_CFS_8_BIT;
    qspi_init.SCPOL = QSPI_CTRL0_SCPOL_LOW;
    qspi_init.SCPH = QSPI_CTRL0_SCPH_FIRST_EDGE;
    qspi_init.FRF = QSPI_CTRL0_FRF_MOTOROLA;
    qspi_init.DFS = QSPI_CTRL0_DFS_8_BIT;
    qspi_init.CLK_DIV = 30U;
    qspi_init.TXFT = QSPI_TXFT_TEI_0;
    qspi_init.RXFT = QSPI_RXFT_TFI_0;
    qspi_init.ENHANCED_ADDR_LEN =
        QSPI_ENH_CTRL0_ADDR_LEN_16_BIT;
    qspi_init.ENHANCED_INST_L =
        QSPI_ENH_CTRL0_INST_L_8_LINE;
    qspi_init.ENHANCED_TRANS_TYPE =
        QSPI_ENH_CTRL0_TRANS_TYPE_ALL_BY_FRF;

    QspiInitConfig(&qspi_init);
    QSPI_Cmd(ENABLE);

    QspiSendWord(0x02U);
    address |= 0x4000U;
    QspiSendWord(address);

    while (count > 0U)
    {
        QspiSendWord(*write_buffer);
        write_buffer++;
        count--;
    }

    if (qspi_wait_checked(requested_address, requested_count,
                          ESC_PDI_ERROR_WRITE_TIMEOUT, 1U) == 0U)
        return 0U;
    qspi_record_success();
    return 1U;
}

uint32_t qspi_read_word(uint16_t address)
{
    UINT32_VAL result = {0};
    (void)qspi_read(result.v, address, 4U);
    return result.Val;
}

static void qspi_delay(uint32_t count)
{
    while (count > 0U)
    {
        __NOP();
        count--;
    }
}

static uint32_t qspi_wait(void)
{
    uint32_t timeout;
    uint32_t start = DWT->CYCCNT;
    uint32_t budget = (SystemCoreClock / 1000000U) * QSPI_PDI_BUSY_TIMEOUT_US;
    uint8_t timed = ((DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk) != 0U) ? 1U : 0U;

    timeout = 0U;
    qspi_delay(5U);

    while (GetQspiBusyStatus())
    {
        timeout++;
        if ((timeout >= QSPI_PDI_BUSY_TIMEOUT_LOOPS) ||
            ((timed != 0U) && ((DWT->CYCCNT - start) >= budget)))
        {
            return 0U;
        }

        qspi_delay(50U);
    }

    return 1U;
}
