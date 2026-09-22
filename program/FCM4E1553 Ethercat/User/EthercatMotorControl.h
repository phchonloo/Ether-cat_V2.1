#ifndef USER_ETHERCAT_MOTOR_CONTROL_H
#define USER_ETHERCAT_MOTOR_CONTROL_H

#include "N32G45XSYS.h"

/* Existing motor-control calibration: BaseRpm = rpm * 16. */
#define ECAT_MOTOR_BASE_UNITS_PER_RPM  16L

/* EtherCAT velocity and position modes share this absolute speed limit. */
#define ECAT_MOTOR_MAX_RPM             1000L

/* Linear command ramps prevent an open-loop stepper from losing synchronism. */
#define ECAT_MOTOR_ACCEL_RPM_PER_SEC   50L
#define ECAT_MOTOR_DECEL_RPM_PER_SEC   100L

/* TIM8 calls the motor algorithm at 16 kHz. */
#define ECAT_MOTOR_CONTROL_HZ          16000L

extern volatile s32 gEcatBaseRpmCommand;
extern volatile u8 gEcatMotorEnabled;

void EthercatMotorControl_Init(void);
void EthercatMotorControl_Update(void);

/* Fixed-rate ramp and feedback helpers called from the 16 kHz motor ISR. */
s32 EthercatMotorControl_ApplyRamp(s32 target_base_rpm);
void EthercatMotorControl_EmergencyStop(void);
void EthercatMotorControl_FastUpdate(s32 applied_base_rpm);

#endif
