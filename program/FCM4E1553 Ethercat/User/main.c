#include "mcuinit.h"

static void eeprom_self_test_report(void);

int main(void)
{
    uint8_t hw_init_result;

    systick_config();
    rcu_config();
    systick_config();

    hw_init_result = HW_Init();

    GpioIni();

    com_gpio_init();
    com_usart_init();

    eeprom_init();
    eeprom_self_test_report();
    eeprom_console_init();
    modbus_init(MODBUS_DEFAULT_SLAVE_ADDR,
                MODBUS_DEFAULT_BAUDRATE);

    /* DMA和ADC就绪后，pwm_config()启动TIM8并开始周期采样。 */
    dma_config();
    adc_config();

    /* PWM启动后，每个完整周期触发一次ADC常规序列和DMA搬运。 */
    pwm_config();
    UpPwm(0U, 0U, 0U, 0U);

    DataIni();
    shuzu_initial();
    IniSaveFunc();

    if (hw_init_result != 0U)
    {
        while (1)
        {
            modbus_process();
            //SciReceive();
            eeprom_console_process();
        }
    }

    (void)MainInit();
    (void)CiA402_Init();

    (void)APPL_GenerateMapping(&nPdInputSize,
                               &nPdOutputSize);

    bRunApplication = TRUE;
    while (bRunApplication == TRUE)
    {
        //SciReceive();
        MainFunt();
        MainLoop();
        modbus_process();
        eeprom_console_process();
    }

    //UpPwm(563U, 1125U, 1688U, 2250U);
    CiA402_DeallocateAxis();
    HW_Release();

    return 0;
}

void rcu_config(void)
{
    /* N32各外设驱动自行开启外设时钟，这里同步系统时钟变量。 */
    SystemCoreClockUpdate();
}

void led_spark(void)
{
}

/**
 * @brief 执行24C02自检，并通过USART1输出自检结果。
 *
 * EEPROM驱动会依次备份测试区域、写入测试数据、校验数据并恢复原数据。
 * 自检使用的状态和提示字符串均放在本函数内，避免主函数保存EEPROM状态。
 */
static void eeprom_self_test_report(void)
{
    eeprom_status_t status;
    uint8_t status_code;
    static const uint8_t start_text[] =
        "[24C02] TEST START\r\n";
    static const uint8_t pass_text[] =
        "[24C02] TEST PASS\r\n";
    static const uint8_t fail_text[] =
        "[24C02] TEST FAIL, STATUS=";
    static const uint8_t line_end[] = "\r\n";

    /* 访问EEPROM前先通知串口调试终端。 */
    (void)uart_write(
        start_text,
        (uint16_t)(sizeof(start_text) - 1U));

    status = eeprom_test();
    if (status == BSP_24C02_OK)
    {
        (void)uart_write(
            pass_text,
            (uint16_t)(sizeof(pass_text) - 1U));
        return;
    }

    /* 当前错误码范围为0～8，可直接转换成一位十进制字符。 */
    status_code = (uint8_t)('0' + (uint8_t)status);
    (void)uart_write(
        fail_text,
        (uint16_t)(sizeof(fail_text) - 1U));
    (void)uart_write(&status_code, 1U);
    (void)uart_write(
        line_end,
        (uint16_t)(sizeof(line_end) - 1U));
}
