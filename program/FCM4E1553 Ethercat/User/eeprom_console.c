#include "mcuinit.h"

#define EEPROM_CONSOLE_LINE_SIZE (80U)
#define EEPROM_CONSOLE_DATA_SIZE (16U)

static uint8_t s_line[EEPROM_CONSOLE_LINE_SIZE];
static uint16_t s_line_length = 0U;
static const uint8_t s_help[] =
    "CMD: A | P | R AA LL | W AA DD...\r\n";

static void eeprom_console_execute(void);
static uint8_t eeprom_console_parse_hex(const uint8_t **text,
                                        uint8_t *value);
static uint8_t eeprom_console_hex_value(uint8_t character,
                                        uint8_t *value);
static uint8_t eeprom_console_upper(uint8_t character);
static void eeprom_console_send_status(eeprom_status_t status);
static void eeprom_console_send_read(const uint8_t *data, uint8_t length);
static void eeprom_console_send_adc(void);
static void eeprom_console_send_adc_values(const uint32_t values[10]);
static void eeprom_console_send_u16(uint16_t value);
static void eeprom_console_send_text(const uint8_t *text, uint16_t length);

void eeprom_console_init(void)
{
    s_line_length = 0U;
    eeprom_console_send_text(s_help, (uint16_t)(sizeof(s_help) - 1U));
}

void eeprom_console_process(void)
{
    uint8_t received[32];
    uint16_t length;
    uint16_t index;

    length = uart_read(received, (uint16_t)sizeof(received));
    for (index = 0U; index < length; index++)
    {
        if ((received[index] == '\r') || (received[index] == '\n'))
        {
            while ((s_line_length != 0U)
                   && (s_line[s_line_length - 1U] == ' '))
            {
                s_line_length--;
            }
            if (s_line_length != 0U)
            {
                s_line[s_line_length] = 0U;
                eeprom_console_execute();
                s_line_length = 0U;
            }
        }
        else if ((received[index] >= 0x20U) && (received[index] <= 0x7EU))
        {
            if ((s_line_length == 0U) && (received[index] == ' '))
            {
                continue;
            }
            if (s_line_length < (EEPROM_CONSOLE_LINE_SIZE - 1U))
            {
                s_line[s_line_length] = received[index];
                s_line_length++;
            }
            else
            {
                s_line_length = 0U;
                eeprom_console_send_text((const uint8_t *)"ERR CMD\r\n", 9U);
            }
        }
    }

}

static void eeprom_console_execute(void)
{
    const uint8_t *text;
    uint8_t address;
    uint8_t count;
    uint8_t data[EEPROM_CONSOLE_DATA_SIZE];
    uint8_t data_length;
    uint8_t command;
    eeprom_status_t status;

    s_line[0] = eeprom_console_upper(s_line[0]);
    s_line[1] = eeprom_console_upper(s_line[1]);

    if (((s_line[0] == 'H') && (s_line[1] == 0U))
        || ((s_line[0] == 'E') && (s_line[1] == 'H')
            && (s_line[2] == 0U)))
    {
        eeprom_console_send_text(s_help,
                                 (uint16_t)(sizeof(s_help) - 1U));
        return;
    }

    if ((s_line[0] == 'A') && (s_line[1] == 0U))
    {
        eeprom_console_send_adc();
        return;
    }

    if (((s_line[0] == 'P') && (s_line[1] == 0U))
        || ((s_line[0] == 'E') && (s_line[1] == 'P')
            && (s_line[2] == 0U)))
    {
        eeprom_console_send_status(eeprom_is_ready());
        return;
    }

    if (s_line[0] == 'E')
    {
        command = s_line[1];
        text = &s_line[2];
    }
    else
    {
        command = s_line[0];
        text = &s_line[1];
    }

    if ((command == 'R')
        && (eeprom_console_parse_hex(&text, &address) != 0U)
        && (eeprom_console_parse_hex(&text, &count) != 0U)
        && (*text == 0U) && (count != 0U)
        && (count <= EEPROM_CONSOLE_DATA_SIZE))
    {
        status = eeprom_read(address, data, count);
        if (status == BSP_24C02_OK)
        {
            eeprom_console_send_read(data, count);
        }
        else
        {
            eeprom_console_send_status(status);
        }
        return;
    }

    if ((command == 'W')
        && (eeprom_console_parse_hex(&text, &address) != 0U))
    {
        data_length = 0U;
        while ((*text != 0U) && (data_length < EEPROM_CONSOLE_DATA_SIZE))
        {
            if (eeprom_console_parse_hex(&text, &data[data_length]) == 0U)
            {
                eeprom_console_send_text((const uint8_t *)"ERR CMD\r\n", 9U);
                return;
            }
            data_length++;
        }
        if ((*text == 0U) && (data_length != 0U))
        {
            eeprom_console_send_status(
                eeprom_write(address, data, data_length));
            return;
        }
    }

    eeprom_console_send_text((const uint8_t *)"ERR CMD\r\n", 9U);
}

static uint8_t eeprom_console_parse_hex(const uint8_t **text,
                                        uint8_t *value)
{
    uint8_t high;
    uint8_t low;

    while (**text == ' ')
    {
        (*text)++;
    }
    if (((*text)[0] == 0U) || ((*text)[1] == 0U))
    {
        return 0U;
    }
    if ((eeprom_console_hex_value((*text)[0], &high) == 0U)
        || (eeprom_console_hex_value((*text)[1], &low) == 0U))
    {
        return 0U;
    }
    if (((*text)[2] != ' ') && ((*text)[2] != 0U))
    {
        return 0U;
    }

    *value = (uint8_t)((high << 4U) | low);
    *text += 2;
    while (**text == ' ')
    {
        (*text)++;
    }
    return 1U;
}

static uint8_t eeprom_console_hex_value(uint8_t character,
                                        uint8_t *value)
{
    character = eeprom_console_upper(character);
    if ((character >= '0') && (character <= '9'))
    {
        *value = (uint8_t)(character - '0');
        return 1U;
    }
    if ((character >= 'A') && (character <= 'F'))
    {
        *value = (uint8_t)(character - 'A' + 10U);
        return 1U;
    }
    return 0U;
}

static uint8_t eeprom_console_upper(uint8_t character)
{
    if ((character >= 'a') && (character <= 'z'))
    {
        character = (uint8_t)(character - 'a' + 'A');
    }
    return character;
}

static void eeprom_console_send_status(eeprom_status_t status)
{
    uint8_t status_code;

    if (status == BSP_24C02_OK)
    {
        eeprom_console_send_text((const uint8_t *)"OK\r\n", 4U);
        return;
    }

    status_code = (uint8_t)('0' + (uint8_t)status);
    eeprom_console_send_text((const uint8_t *)"ERR ", 4U);
    (void)uart_write(&status_code, 1U);
    eeprom_console_send_text((const uint8_t *)"\r\n", 2U);
}

static void eeprom_console_send_read(const uint8_t *data, uint8_t length)
{
    static const uint8_t hex[] = "0123456789ABCDEF";
    uint8_t output[3];
    uint8_t index;

    eeprom_console_send_text((const uint8_t *)"OK", 2U);
    for (index = 0U; index < length; index++)
    {
        output[0] = ' ';
        output[1] = hex[data[index] >> 4U];
        output[2] = hex[data[index] & 0x0FU];
        (void)uart_write(output, 3U);
    }
    eeprom_console_send_text((const uint8_t *)"\r\n", 2U);
}

static void eeprom_console_send_adc(void)
{
    eeprom_console_send_adc_values(adc_value);
}

static void eeprom_console_send_adc_values(const uint32_t values[10])
{
    uint8_t index;

    eeprom_console_send_text((const uint8_t *)"ADC", 3U);
    for (index = 0U; index < 10U; index++)
    {
        eeprom_console_send_text((const uint8_t *)" ", 1U);
        eeprom_console_send_u16((uint16_t)values[index]);
    }
    eeprom_console_send_text((const uint8_t *)"\r\n", 2U);
}

static void eeprom_console_send_u16(uint16_t value)
{
    uint8_t digits[5];
    uint8_t output[5];
    uint8_t length;
    uint8_t index;

    length = 0U;
    do
    {
        digits[length] = (uint8_t)('0' + (value % 10U));
        value = (uint16_t)(value / 10U);
        length++;
    } while (value != 0U);

    for (index = 0U; index < length; index++)
    {
        output[index] = digits[length - index - 1U];
    }
    (void)uart_write(output, length);
}

static void eeprom_console_send_text(const uint8_t *text, uint16_t length)
{
    (void)uart_write(text, length);
}
