#include "qspi.h"

#define QSPI_PDI_BUSY_TIMEOUT_LOOPS (1000U)

static volatile uint32_t s_qspi_timeout_count;

static void qspi_delay(uint32_t count);
static uint32_t qspi_wait(void);

/**
 * @brief Wait for an idle QSPI bus and record a timeout for debugging.
 */
static void qspi_wait_and_record(void)
{
    if (qspi_wait() == 0U)
    {
        s_qspi_timeout_count++;
    }
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

void qspi_read(uint8_t *read_buffer,
               uint16_t address,
               uint8_t count)
{
    QSPI_InitType qspi_init;
    uint32_t wait_cycles;

    wait_cycles = (uint32_t)(address & 0x0003U) *
                  0x1000U;
    qspi_wait_and_record();

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

    while (count > 0U)
    {
        qspi_wait_and_record();
        *read_buffer = (uint8_t)QspiReadWord();
        read_buffer++;
        count--;
    }
}

void qspi_write(uint8_t *write_buffer,
                uint16_t address,
                uint16_t count)
{
    QSPI_InitType qspi_init = {0};

    qspi_wait_and_record();

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

    qspi_wait_and_record();
}

uint32_t qspi_read_word(uint16_t address)
{
    UINT32_VAL result;
    UINT16_VAL mapped_address;

    mapped_address.Val = address;

    QspiSendWord(0x0BU);
    QspiSendWord(mapped_address.byte.HB | 0x40U);
    QspiSendWord(mapped_address.byte.LB);
    QspiSendWord(4U);
    QspiSendWord(0U);

    QspiSendWord(0U);
    result.byte.LB = (uint8_t)QspiReadWord();
    QspiSendWord(0U);
    result.byte.HB = (uint8_t)QspiReadWord();
    QspiSendWord(0U);
    result.byte.UB = (uint8_t)QspiReadWord();
    QspiSendWord(0U);
    result.byte.MB = (uint8_t)QspiReadWord();

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

    timeout = 0U;
    qspi_delay(5U);

    while (GetQspiBusyStatus())
    {
        timeout++;
        if (timeout >= QSPI_PDI_BUSY_TIMEOUT_LOOPS)
        {
            return 0U;
        }

        qspi_delay(50U);
    }

    return 1U;
}
