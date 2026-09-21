#ifndef USER_ETHERCAT_MOTOR_CONTROL_H
#define USER_ETHERCAT_MOTOR_CONTROL_H

#include "N32G45XSYS.h"

/* Existing motor-control calibration: BaseRpm 480 produces 30 rpm. */
#define ECAT_MOTOR_BASE_UNITS_PER_RPM  16L

/* Keep the first fieldbus test within the already verified 30 rpm envelope. */
#define ECAT_MOTOR_MAX_RPM             30L

extern volatile s32 gEcatBaseRpmCommand;
extern volatile u8 gEcatMotorEnabled;

void EthercatMotorControl_Init(void);
void EthercatMotorControl_Update(void);

#endif
