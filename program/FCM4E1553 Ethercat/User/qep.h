#ifndef USER_QEP_H
#define USER_QEP_H

#include "N32G45XSYS.h"

/* Legacy implementation retained on disk only; qep.c is not built. */

/**
 * @brief 初始化PA0、PA1、PA2上的TIM2硬件编码器接口。
 *
 * PA0=X1/TIM2_CH1/A相，PA1=X2/TIM2_CH2/B相，
 * PA2=X3/TIM2_CH3/Z相。启用QEP后X1～X3不再作为普通数字输入使用。
 */
void qep_init(void);

/**
 * @brief 累加一个控制周期内的TIM2编码器计数变化。
 * @note 由ADC完成中断以16 kHz频率调用。
 */
void qep_update(void);

/**
 * @brief 设置新的32位编码器位置。
 * @param[in] position 新位置值。
 * @note 只在电机停止或已经确认Z相位置时调用。
 */
void qep_reset(int32_t position);

/**
 * @brief 读取累计的32位编码器位置。
 */
int32_t qep_get_position(void);

/**
 * @brief 读取最近一个16 kHz周期内的编码器计数变化量。
 */
int16_t qep_get_delta(void);

/**
 * @brief 读取并清除一次Z相捕获结果。
 * @param[out] position Z相下降沿出现时的32位位置。
 * @return 1表示有新的Z相结果，0表示没有新结果。
 */
uint8_t qep_get_index(int32_t *position);

#endif
