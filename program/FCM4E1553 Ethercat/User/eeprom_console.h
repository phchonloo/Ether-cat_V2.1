#ifndef USER_EEPROM_CONSOLE_H
#define USER_EEPROM_CONSOLE_H

#include "N32G45XSYS.h"

/** @brief Clear the USART1 command line buffer. */
void eeprom_console_init(void);

/**
 * @brief Process USART1 commands for the 24C02 from the foreground loop.
 * @note Commands are executed only after CR or LF is received.
 */
void eeprom_console_process(void);

#endif
