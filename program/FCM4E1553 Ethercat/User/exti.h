#ifndef USER_EXTI_H
#define USER_EXTI_H

#include "N32G45XSYS.h"

/** @brief Configure PB0/EXTI0 for the active-low ESC IRQ signal. */
void exti_init_irq(void);

/** @brief Configure PB1/EXTI1 for the active-low EtherCAT SYNC0 signal. */
void exti_init_sync0(void);

/** @brief Configure PB2/EXTI2 for the active-low EtherCAT SYNC1 signal. */
void exti_init_sync1(void);

#endif
