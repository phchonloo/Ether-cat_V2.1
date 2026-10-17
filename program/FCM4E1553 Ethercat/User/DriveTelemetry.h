#ifndef DRIVE_TELEMETRY_H
#define DRIVE_TELEMETRY_H
#include <stdint.h>

/* Measurement validity, NOT a drive enable or safety signal. */
#define DT_VALID_VOLTAGE 0x0001U
#define DT_VALID_PHASES  0x0002U
#define DT_VALID_CURRENT 0x0004U
#define DT_VALID_TORQUE  0x0008U /* not supported on this open-loop board */
#define DT_VALID_BUS_CURRENT 0x0010U /* no ADC connected to bus shunt */
#define DT_VALID_TRACKING 0x0020U /* internal position error, PP/CSP only */
#define DT_ADC_STALE     0x0100U
#define DT_ADC_ERROR     0x0200U
#define DT_ADC_RAIL      0x0400U
#define DT_CURRENT_UNCALIBRATED 0x0800U
#define DT_RATED_UNSET   0x1000U
#define DT_INVALID_I16 (-32767 - 1)
#define DT_INVALID_I32 (-2147483647L - 1L)
#define DT_INVALID_U32 0xFFFFFFFFUL

typedef struct {
    uint16_t status; /* 0x2001 */
    int32_t busCurrentMa; /* 0x2002 */
    int32_t phaseAMa; /* 0x2003 */
    int32_t phaseBMa; /* 0x2004 */
    uint16_t rawBus; /* 0x2005 */
    uint16_t rawA; /* 0x2006 */
    uint16_t rawB; /* 0x2007 */
    uint32_t sampleCount; /* 0x2008 */
    int16_t currentMa; /* 0x2064 */
    uint16_t busVoltageDecivolt; /* 0x2065 */
    uint16_t runState; /* 0x206D */
    uint16_t hardwareVersion; /* 0x206E */
    uint16_t softwareVersion; /* 0x206F */
    int16_t currentPermille; /* 0x6078 */
    uint32_t busVoltageMv; /* 0x6079 */
    int32_t followingError; /* 0x60F4 */
} DriveTelemetryValues;
typedef struct {
    uint32_t vrefMv; /* 0x2010 */
    uint32_t sensitivityUvA; /* 0x2011 */
    uint32_t zeroAUv; /* 0x2012 */
    uint32_t zeroBUv; /* 0x2013 */
    uint32_t ratedCurrentMa; /* 0x6075 */
} DriveTelemetryConfig;
extern DriveTelemetryValues gDriveTelemetry;
extern DriveTelemetryConfig gDriveTelemetryConfig;

void DriveTelemetry_Init(void);
void DriveTelemetry_AdcComplete(void);
void DriveTelemetry_AdcError(void);
void DriveTelemetry_Update(void);
/* Copy one immutable foreground-published snapshot. No waits in EtherCAT ISR. */
void DriveTelemetry_Latch(void);
void DriveTelemetry_GetSnapshot(DriveTelemetryValues *snapshot);
#endif
