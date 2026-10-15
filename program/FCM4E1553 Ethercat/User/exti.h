#ifndef USER_EXTI_H
#define USER_EXTI_H

#include "N32G45XSYS.h"

/* This board wires ESC IRQ and SYNC0 only; PB2/SYNC1 is not connected. */
#define ECAT_SYNC1_IRQ_ENABLED 0U

/** @brief Configure PB0/EXTI0 for the active-low ESC IRQ signal. */
void exti_init_irq(void);

/** @brief Configure PB1/EXTI1 for the active-low EtherCAT SYNC0 signal. */
void exti_init_sync0(void);

/** @brief Configure SYNC1 if wired; otherwise mask and clear EXTI2/NVIC. */
void exti_init_sync1(void);

#endif
