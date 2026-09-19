#ifndef USER_IO_INT_INIT_H
#define USER_IO_INT_INIT_H

#include "mcuinit.h"

#define IO_INPUT_COUNT  (4U)
#define IO_OUTPUT_COUNT (4U)

/** 四路板级数字输入X1～X4，外部光耦导通时为有效。 */
typedef enum
{
    IO_INPUT_X1 = 0,
    IO_INPUT_X2,
    IO_INPUT_X3,
    IO_INPUT_X4
} io_input_t;

/** 四路板级数字输出Y1～Y4。 */
typedef enum
{
    IO_OUTPUT_Y1 = 0,
    IO_OUTPUT_Y2,
    IO_OUTPUT_Y3,
    IO_OUTPUT_Y4
} io_output_t;

/**
 * @brief 初始化新原理图上的四路数字输入和四路数字输出。
 *
 * 输入分配：X1=PA0、X2=PA1、X3=PA2、X4=PA3。
 * 输出分配：Y1=PC10、Y2=PC11、Y3=PC12、Y4=PA15。
 * 输入和输出光耦均为低电平有效；初始化后所有输出保持关闭。
 * QEP接口未接入运行流程，X1～X4保持为普通数字输入。
 */
void GpioIni(void);

/*
 * STD245S motor-algorithm compatibility interface.
 *
 * The pulse/direction interrupt entry points configure TIM2 capture only
 * when they are explicitly enabled.  Otherwise X1/PA0 and X2/PA1 remain
 * ordinary GPIO inputs.  X3/PA2 is no longer reserved by QEP.
 */
u8 XiFen(void);
u8 DianLiu(void);
void IoPuIntEnable(void);
void IoDrIntEnable(void);
void IointDisable(void);
void IoCaptureHandle(void);
u8 IoPuGet(void);
u8 IoDrGet(void);
u8 IoModeGet(void);
u8 IoMfGet(void);
u8 IoVfoGet(void);
void LedRunSet(u8 temdata);
void LedErrSet(u8 temdata);

/**
 * @brief 读取一路数字输入的逻辑状态。
 * @param[in] input 输入编号IO_INPUT_X1～IO_INPUT_X4。
 * @return 1表示外部输入有效，0表示无效或参数越界。
 */
uint8_t io_read_input(io_input_t input);

/**
 * @brief 控制一路数字输出。
 * @param[in] output 输出编号IO_OUTPUT_Y1～IO_OUTPUT_Y4。
 * @param[in] active 1打开输出光耦，0关闭输出光耦。
 */
void io_write_output(io_output_t output, uint8_t active);

/**
 * @brief 读取一路数字输出当前的逻辑状态。
 * @param[in] output 输出编号IO_OUTPUT_Y1～IO_OUTPUT_Y4。
 * @return 1表示输出光耦已打开，0表示关闭或参数越界。
 */
uint8_t io_read_output(io_output_t output);

#endif
