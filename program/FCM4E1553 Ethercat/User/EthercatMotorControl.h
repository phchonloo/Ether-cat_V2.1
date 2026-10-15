#ifndef USER_ETHERCAT_MOTOR_CONTROL_H
#define USER_ETHERCAT_MOTOR_CONTROL_H

#include "N32G45XSYS.h"

/* Existing motor-control calibration: BaseRpm = rpm * 16. */
#define ECAT_MOTOR_BASE_UNITS_PER_RPM  16L

/* EtherCAT velocity and position modes share this absolute speed limit. */
#define ECAT_MOTOR_MAX_RPM             1000L
#define ECAT_MOTOR_DEFAULT_PROFILE_RPM 100L

/* Default and maximum configurable linear command ramps, in rpm/s. */
#define ECAT_MOTOR_ACCEL_RPM_PER_SEC   300L
#define ECAT_MOTOR_DECEL_RPM_PER_SEC   300L
#define ECAT_MOTOR_MAX_RAMP_RPM_PER_SEC 10000L

/* TIM8 calls the motor algorithm at 16 kHz. */
#define ECAT_MOTOR_CONTROL_HZ          16000L

extern volatile s32 gEcatBaseRpmCommand;
extern volatile u8 gEcatMotorEnabled;

typedef struct
{
    s32 countedPosition;
    s32 targetPosition;
    s32 resolution;
    s32 lastPhaseStep;
    s32 requestedPhaseStep;
    uint32_t countedCycles;
    uint32_t skippedCycles;
    uint32_t mismatchCount;
    uint32_t positiveStepCycles;
    uint32_t negativeStepCycles;
    uint32_t blockedAwayCount;
    uint32_t maxStepDelta;
    uint8_t atTarget;
    uint8_t positionLimit;
    uint8_t resolutionChanged;
} TEcatPulseDiagnostics;
extern volatile TEcatPulseDiagnostics gEcatPulseDiag;

void EthercatMotorControl_Init(void);
void EthercatMotorControl_Update(void);
/* Latch one coherent setpoint after SM RxPDO mapping or on DC Sync0. */
void EthercatMotorControl_OnSyncCycle(void);
/* Emitted phase/ramp standstill only; NOT a measured shaft standstill. */
u8 EthercatMotorControl_IsStopped(void);
/* Final gate for every PWM write, including writes from the vendor library. */
u8 EthercatMotorControl_PowerAllowed(void);

/* Fixed-rate ramp and feedback helpers called from the 16 kHz motor ISR. */
s32 EthercatMotorControl_ApplyRamp(s32 target_base_rpm);
void EthercatMotorControl_EmergencyStop(void);
/* Called around MainFunt: prepare a bounded step, then count actual PhaseGet. */
s32 EthercatMotorControl_NextPhaseStep(void);
void EthercatMotorControl_BeginPhaseCycle(s32 angle);
void EthercatMotorControl_EndPhaseCycle(s32 angle);

#endif
