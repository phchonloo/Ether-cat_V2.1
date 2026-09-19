#ifndef USER_PWM_INIT_H
#define USER_PWM_INIT_H

#include "mcuinit.h"

extern u8 PwmFlag;

/**
 * @brief 初始化并启动四路同步、独立占空比的PWM输出。
 *
 * TIM8产生16 kHz中心对齐PWM。PC6=A-、PC7=A+、PC8=B-、PC9=B+，
 * 分别对应TIM8的CH1、CH2、CH3、CH4。TIM8更新TRGO直接触发ADC3
 * 常规序列。
 *
 * @note 四路比较值初始为0，未配置互补逻辑和死区。
 */
void pwm_config(void);

/**
 * @brief 直接写入四路TIM8比较值。
 * @param[in] pwm1 PC6/A-对应的TIM8_CH1比较值。
 * @param[in] pwm2 PC7/A+对应的TIM8_CH2比较值。
 * @param[in] pwm3 PC8/B-对应的TIM8_CH3比较值。
 * @param[in] pwm4 PC9/B+对应的TIM8_CH4比较值。
 * @note 当前PWM有效比较值范围为0～2250，调用方应保证参数有效。
 */
void UpPwm(u16 pwm1, u16 pwm2, u16 pwm3, u16 pwm4);

#endif
