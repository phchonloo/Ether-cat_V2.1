#ifndef USER_TIMER_H
#define USER_TIMER_H

#include "N32G45XSYS.h"

/**
 * @brief 初始化EtherCAT协议栈使用的TIM6本地时基。
 * @param[in] period 原协议栈使用的定时周期倍数。
 * @note ECAT_TIMER_INT非零时才打开TIM6更新中断。
 */
void timer_init(uint8_t period);

/**
 * @brief 初始化PWM和ADC共同使用的TIM8时基。
 *
 * TIM8产生16 kHz中心对齐PWM，并用更新TRGO直接触发ADC3常规序列。
 * 本函数只完成配置，不启动计数器。
 */
void timer_pwm_adc_init(void);

/**
 * @brief 启动PWM和ADC共同使用的TIM8计数器。
 * @note 调用前必须先完成ADC和PWM通道配置。
 */
void timer_pwm_adc_start(void);

#endif
