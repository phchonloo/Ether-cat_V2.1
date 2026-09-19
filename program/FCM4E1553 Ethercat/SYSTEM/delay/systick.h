#ifndef SYSTICK_H
#define SYSTICK_H
#include <sys.h>	  

#if SYSTEM_SUPPORT_OS
#include "includes.h"
#endif

void systick_config(void);

/* STD245S-compatible millisecond-delay entry points. */
void delay_1ms(uint32_t count);
void delay_decrement(void);

/**
 * @brief Initialize the SysTick-based blocking delay service.
 * @param[in] SYSCLK Core clock frequency expressed in MHz.
 * @note Call once after the system clock is configured.
 */
void delay_init(u8 SYSCLK);

/**
 * @brief Block for the requested number of milliseconds.
 * @param[in] nms Delay duration in milliseconds.
 */
void delay_ms(u16 nms);

/**
 * @brief Block for the requested number of microseconds.
 * @param[in] nus Delay duration in microseconds, limited by the 24-bit SysTick.
 */
void delay_us(u32 nus);

/**
 * @brief Execute one SysTick-sized millisecond delay segment.
 * @param[in] nms Segment duration in milliseconds.
 * @note delay_ms() should normally be used because it splits long delays.
 */
void delay_xms(u16 nms);

#endif





























