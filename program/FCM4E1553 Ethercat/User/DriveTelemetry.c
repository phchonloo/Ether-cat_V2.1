#include "DriveTelemetry.h"
#include "AdcInit.h"
#include "EthercatMotorControl.h"
#include "cia402appl.h"
#include "coeappl.h"
extern TCiA402Axis LocalAxes[MAX_AXES];

DriveTelemetryValues gDriveTelemetry;
DriveTelemetryConfig gDriveTelemetryConfig = {
    DT_DEFAULT_VREF_MV, DT_DEFAULT_SENSITIVITY_UVA,
    DT_DEFAULT_ZERO_UV, DT_DEFAULT_ZERO_UV, 0U
};

typedef struct {
    uint32_t sequence, cycle;
    uint16_t a, b, bus, rails;
} RawFrame;
typedef struct {
    DriveTelemetryValues values;
    uint32_t cycle;
} ConvertedFrame;
static volatile RawFrame sRaw[2];
static volatile uint32_t sRawIndex, sRawCount, sAdcError;
static ConvertedFrame sConverted[2];
static volatile uint32_t sConvertedIndex;
static uint32_t sLastUpdate;
static volatile uint32_t sExpiredSequence;

static void InvalidMeasurements(DriveTelemetryValues *v)
{
    v->status &= ~(DT_VALID_VOLTAGE | DT_VALID_PHASES | DT_VALID_CURRENT |
                   DT_VALID_TORQUE | DT_VALID_BUS_CURRENT);
    v->phaseAMa = v->phaseBMa = v->busCurrentMa = DT_INVALID_I32;
    v->currentMa = v->currentPermille = DT_INVALID_I16;
    v->busVoltageMv = DT_INVALID_U32;
    v->busVoltageDecivolt = 0xFFFFU;
}

void DriveTelemetry_Init(void)
{
    memset((void *)sRaw, 0, sizeof(sRaw));
    memset(sConverted, 0, sizeof(sConverted));
    sRawIndex = sRawCount = sAdcError = sConvertedIndex = sLastUpdate = 0U;
    sExpiredSequence = 0U;
    gDriveTelemetryConfig.vrefMv = DT_DEFAULT_VREF_MV;
    /* MT9221 Rev 2.0, p9: +/-10 A, 200 mV/A, Vzero=VCC/2 at 5 V.
     * User-authorized substitute profile only; verify the fitted sensor,
     * supply, VDDA and both zero offsets before trusting physical accuracy. */
    gDriveTelemetryConfig.sensitivityUvA = DT_DEFAULT_SENSITIVITY_UVA;
    gDriveTelemetryConfig.zeroAUv = gDriveTelemetryConfig.zeroBUv = DT_DEFAULT_ZERO_UV;
    /* Sensor full scale is NOT the motor's rated/reference current. */
    gDriveTelemetryConfig.ratedCurrentMa = 0U;
    sConverted[0].values.status = DT_ADC_STALE | DT_RATED_UNSET;
    if (gDriveTelemetryConfig.sensitivityUvA == 0U)
        sConverted[0].values.status |= DT_CURRENT_UNCALIBRATED;
    InvalidMeasurements(&sConverted[0].values);
    sConverted[0].values.hardwareVersion = 0x0102U;
    sConverted[0].values.softwareVersion = 0x0100U;
    sConverted[0].values.followingError = DT_INVALID_I32;
    sConverted[1] = sConverted[0];
    DriveTelemetry_Latch();
}

void DriveTelemetry_AdcError(void)
{
    sAdcError = 1U;
}

void DriveTelemetry_AdcComplete(void)
{
    RawFrame f;
    uint32_t i, a = 0U, b = 0U, start = DWT->CYCCNT;
    uint32_t slot = sRawIndex ^ 1U;
    /* DMA has reloaded NDTR=10 at transfer completion. If service was late,
     * never combine samples from two conversions. IRQ/SYNC0 stay unmasked. */
    if (DMA_GetCurrDataCounter(DMA2_CH1) != 10U) return;
    f.rails = 0U;
    for (i = 0U; i < 10U; ++i)
    {
        uint32_t v = ((volatile uint32_t *)adc_value)[i];
        if (i == 2U) {
            f.bus = (uint16_t)v;
            if (v >= 4095U) f.rails |= 1U;
        } else if (i != 3U) {
            if ((v == 0U) || (v >= 4095U)) f.rails |= 2U;
            if ((i & 1U) == 0U) a += v; else b += v;
        }
    }
    /* Also reject a preemption long enough for an entire DMA buffer wrap. */
    if ((DMA_GetCurrDataCounter(DMA2_CH1) != 10U) ||
        ((uint32_t)(DWT->CYCCNT - start) >= SystemCoreClock / 32000U)) return;
    f.a = (uint16_t)((a + 2U) / 4U);
    f.b = (uint16_t)((b + 2U) / 4U);
    f.sequence = sRawCount + 1U;
    if (f.sequence == 0U) f.sequence = 1U;
    f.cycle = start;
    sRaw[slot] = f;
    __DMB();
    sRawIndex = slot;
    sRawCount = f.sequence;
    sAdcError = 0U;
}

static uint32_t IntegerSqrt(uint32_t n)
{
    uint32_t result = 0U, bit = 1UL << 30;
    while (bit != 0U) {
        if (n >= result + bit) { n -= result + bit; result = (result >> 1) + bit; }
        else result >>= 1;
        bit >>= 2;
    }
    return result;
}

static int32_t PhaseMa(uint16_t raw, uint32_t zero)
{
    int64_t uv = (int64_t)raw * gDriveTelemetryConfig.vrefMv * 1000 / 4095;
    /* R101/R224=10k, R100/R225=20k: ADC sees 2/3 of sensor output. */
    return (int32_t)((uv - zero) * 3000 / (2 * (int64_t)gDriveTelemetryConfig.sensitivityUvA));
}

void DriveTelemetry_Update(void)
{
    RawFrame raw;
    ConvertedFrame *c;
    DriveTelemetryValues *v;
    uint32_t count, now = DWT->CYCCNT, slot;
    int64_t error;
    if ((uint32_t)(now - sLastUpdate) < SystemCoreClock / 1000U) {
        DriveTelemetry_Latch();
        return;
    }
    sLastUpdate = now;
    count = sRawCount;
    __DMB();
    raw = sRaw[sRawIndex];
    __DMB();
    /* A foreground copy may be interrupted by DMA. Retry next loop, not spin. */
    if ((count != sRawCount) || (count != raw.sequence)) return;
    slot = sConvertedIndex ^ 1U;
    c = &sConverted[slot];
    memset(c, 0, sizeof(*c));
    c->cycle = raw.cycle;
    v = &c->values;
    InvalidMeasurements(v);
    v->rawA = raw.a; v->rawB = raw.b; v->rawBus = raw.bus;
    v->sampleCount = raw.sequence;
    if (gDriveTelemetryConfig.sensitivityUvA == 0U) v->status |= DT_CURRENT_UNCALIBRATED;
    if (gDriveTelemetryConfig.ratedCurrentMa == 0U) v->status |= DT_RATED_UNSET;
    if (raw.rails) v->status |= DT_ADC_RAIL;
    if ((raw.sequence != 0U) && (raw.sequence != sExpiredSequence) && !sAdcError &&
        ((uint32_t)(now - raw.cycle) < SystemCoreClock / 200U)) {
        if ((raw.rails & 1U) == 0U) {
            /* PE12/ADC2, R99=100k and R104=2k, nominal VDDA=3.3V. */
            v->busVoltageMv = ((uint32_t)raw.bus * gDriveTelemetryConfig.vrefMv * 51U + 2047U) / 4095U;
            v->busVoltageDecivolt = (uint16_t)((v->busVoltageMv + 50U) / 100U);
            v->status |= DT_VALID_VOLTAGE;
        }
        if (((raw.rails & 2U) == 0U) && (gDriveTelemetryConfig.sensitivityUvA != 0U)) {
            int32_t a = PhaseMa(raw.a, gDriveTelemetryConfig.zeroAUv);
            int32_t b = PhaseMa(raw.b, gDriveTelemetryConfig.zeroBUv);
            /* Software monitoring range +/-10 A, NOT an overcurrent trip.
             * Out-of-range values are never presented as valid clamps. */
            if ((a >= -10000) && (a <= 10000) && (b >= -10000) && (b <= 10000)) {
                uint32_t magnitude = IntegerSqrt((uint32_t)(a * a + b * b));
                v->phaseAMa = a; v->phaseBMa = b;
                v->currentMa = (int16_t)magnitude;
                v->status |= DT_VALID_PHASES;
                if (gDriveTelemetryConfig.ratedCurrentMa != 0U) {
                    uint32_t p = magnitude * 1000U / gDriveTelemetryConfig.ratedCurrentMa;
                    if (p <= 32767U) { v->currentPermille = (int16_t)p; v->status |= DT_VALID_CURRENT; }
                }
            } else v->status |= DT_ADC_RAIL;
        }
    } else {
        v->status |= DT_ADC_STALE;
        sExpiredSequence = raw.sequence;
    }
    if (sAdcError) v->status |= DT_ADC_ERROR;
    v->runState = LocalAxes[0].Objects.objStatusWord;
    v->hardwareVersion = 0x0102U;
    v->softwareVersion = 0x0100U;
    v->followingError = DT_INVALID_I32;
    if ((LocalAxes[0].Objects.objModesOfOperationDisplay == 1) ||
        (LocalAxes[0].Objects.objModesOfOperationDisplay == 8)) {
        error = (int64_t)gEcatPulseDiag.targetPosition - gEcatPulseDiag.countedPosition;
        if (error > 2147483647LL) error = 2147483647LL;
        if (error < -2147483647LL) error = -2147483647LL;
        v->followingError = (int32_t)error;
        v->status |= DT_VALID_TRACKING;
    }
    __DMB();
    sConvertedIndex = slot;
    DriveTelemetry_Latch();
}

void DriveTelemetry_Latch(void)
{
    DriveTelemetry_GetSnapshot(&gDriveTelemetry);
}

void DriveTelemetry_GetSnapshot(DriveTelemetryValues *snapshot)
{
    const ConvertedFrame *c = &sConverted[sConvertedIndex];
    __DMB();
    /* Foreground is the only converted-slot writer. Each SDO/PDO reader gets
     * its own snapshot, including during nested IRQ/SYNC0 handling. */
    *snapshot = c->values;
    if ((c->values.sampleCount == 0U) ||
        (c->values.sampleCount == sExpiredSequence) || sAdcError ||
        ((uint32_t)(DWT->CYCCNT - c->cycle) >= SystemCoreClock / 200U)) {
        sExpiredSequence = c->values.sampleCount;
        InvalidMeasurements(snapshot);
        snapshot->status |= DT_ADC_STALE;
        if (sAdcError) snapshot->status |= DT_ADC_ERROR;
    }
}

UINT8 ReadDriveTelemetry(UINT16 index, UINT8 sub, UINT32 size,
                        UINT16 MBXMEM *data, UINT8 complete)
{
    const TOBJECT *object;
    DriveTelemetryValues snapshot;
    const void *source;
    uint16_t required = 0U;
    if (complete) return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (sub != 0U) return ABORTIDX_SUBINDEX_NOT_EXISTING;
    object = OBJ_GetObjectHandle(index);
    if ((object == NULL) || (object->pVarPtr == NULL)) return ABORTIDX_OBJECT_NOT_EXISTING;
    if (size != object->pEntryDesc->BitLength / 8U) return ABORTIDX_PARAM_LENGTH_ERROR;
    DriveTelemetry_GetSnapshot(&snapshot);
    switch (index) {
    case 0x6077: case 0x2002: return ABORTIDX_HARDWARE_ERROR;
    case 0x6079: case 0x2065: required = DT_VALID_VOLTAGE; break;
    case 0x2003: case 0x2004: case 0x2064: required = DT_VALID_PHASES; break;
    case 0x6078: required = DT_VALID_CURRENT; break;
    case 0x60F4: required = DT_VALID_TRACKING; break;
    default: break;
    }
    if (required && !(snapshot.status & required)) return ABORTIDX_DATA_CANNOT_BE_READ_OR_STORED;
    source = object->pVarPtr;
    if (((uintptr_t)source >= (uintptr_t)&gDriveTelemetry) &&
        ((uintptr_t)source + size <= (uintptr_t)&gDriveTelemetry + sizeof(gDriveTelemetry)))
        source = (const uint8_t *)&snapshot + ((uintptr_t)source - (uintptr_t)&gDriveTelemetry);
    MEMCPY(data, source, size);
    return 0U;
}

UINT8 WriteDriveTelemetry(UINT16 index, UINT8 sub, UINT32 size,
                         UINT16 MBXMEM *data, UINT8 complete)
{
    uint32_t value;
    uint32_t *target;
    if (complete) return ABORTIDX_COMPLETE_ACCESS_NOT_SUPPORTED;
    if (sub != 0U) return ABORTIDX_SUBINDEX_NOT_EXISTING;
    if (size != 4U) return ABORTIDX_PARAM_LENGTH_ERROR;
    if (((nAlStatus & STATE_MASK) != STATE_PREOP) || gEcatMotorEnabled ||
        !EthercatMotorControl_IsStopped()) return ABORTIDX_IN_THIS_STATE_DATA_CANNOT_BE_READ_OR_STORED;
    value = SWAPWORD(data[0]) | ((uint32_t)SWAPWORD(data[1]) << 16U);
    switch (index) {
    case 0x2010:
        if ((value < 2800U) || (value > 3600U)) return ABORTIDX_VALUE_EXCEEDED;
        target = &gDriveTelemetryConfig.vrefMv; break;
    case 0x2011:
        if ((value != 0U) && ((value < 10000U) || (value > 1000000U))) return ABORTIDX_VALUE_EXCEEDED;
        target = &gDriveTelemetryConfig.sensitivityUvA; break;
    case 0x2012: case 0x2013:
        if ((value < 500000U) || (value > 2800000U)) return ABORTIDX_VALUE_EXCEEDED;
        target = (index == 0x2012) ? &gDriveTelemetryConfig.zeroAUv : &gDriveTelemetryConfig.zeroBUv; break;
    case 0x6075:
        if ((value != 0U) && ((value < 100U) || (value > 10000U))) return ABORTIDX_VALUE_EXCEEDED;
        target = &gDriveTelemetryConfig.ratedCurrentMa; break;
    default: return ABORTIDX_UNSUPPORTED_ACCESS;
    }
    *target = value;
    /* Immediately invalidate readings from the previous calibration. */
    sConverted[sConvertedIndex].values.sampleCount = 0U;
    sLastUpdate = DWT->CYCCNT - SystemCoreClock / 1000U;
    DriveTelemetry_Latch();
    return 0U;
}
