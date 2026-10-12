#ifndef ECAT_PULSE_POSITION_H
#define ECAT_PULSE_POSITION_H

#include <stdint.h>

/* N32G455.lib PhaseGet adds BaseRpm to Iangle and wraps at 307200.
 * At 16 kHz with BaseRpm=rpm*16, a revolution is 15360000 units.
 * These are excitation counts, NOT STEP-pin pulses or encoder feedback. */
#define ECAT_PHASE_UNITS_PER_REV   15360000L
#define ECAT_PHASE_UNITS_PER_CYCLE 307200L
#define ECAT_PHASE_MAX_INCREMENT   16000L
#define ECAT_PULSE_MAX_RESOLUTION   40000L

typedef struct
{
    int64_t phase;
    int64_t minimum_phase;
    int64_t maximum_phase;
    int32_t whole_pulses;
    int32_t fraction;
    int32_t resolution;
} TEcatPulsePosition;

static int64_t EcatPulse_TargetPhase(int32_t pulses, int32_t resolution)
{
    int64_t scaled = (int64_t)pulses * ECAT_PHASE_UNITS_PER_REV;
    if (scaled >= 0)
        scaled += resolution / 2;
    else
        scaled -= resolution / 2;
    return scaled / resolution;
}

static void EcatPulse_Reset(TEcatPulsePosition *position, int32_t resolution)
{
    position->phase = 0;
    position->whole_pulses = 0;
    position->fraction = 0;
    position->resolution = resolution;
    position->minimum_phase = EcatPulse_TargetPhase(INT32_MIN, resolution);
    position->maximum_phase = EcatPulse_TargetPhase(INT32_MAX, resolution);
}

static int32_t EcatPulse_Position(const TEcatPulsePosition *position)
{
    int64_t rounded = position->whole_pulses;
    if (position->fraction >= ECAT_PHASE_UNITS_PER_REV / 2)
        rounded++;
    else if (position->fraction <= -ECAT_PHASE_UNITS_PER_REV / 2)
        rounded--;
    if (rounded > INT32_MAX)
        return INT32_MAX;
    if (rounded < INT32_MIN)
        return INT32_MIN;
    return (int32_t)rounded;
}

static int32_t EcatPulse_LimitStep(const TEcatPulsePosition *position,
                                 int64_t target, int32_t step,
                                 uint8_t position_mode)
{
    int64_t remaining;
    int64_t boundary;
    if (step > ECAT_PHASE_MAX_INCREMENT)
        step = ECAT_PHASE_MAX_INCREMENT;
    else if (step < -ECAT_PHASE_MAX_INCREMENT)
        step = -ECAT_PHASE_MAX_INCREMENT;
    if (position_mode != 0U)
    {
        remaining = target - position->phase;
        if (remaining == 0)
            return 0;
        /* Braking away from a newly reversed target is permitted.
         * Travel toward the target must never cross its final count. */
        if ((step > 0) && (remaining > 0) && (remaining < step))
            step = (int32_t)remaining;
        else if ((step < 0) && (remaining < 0) && (remaining > step))
            step = (int32_t)remaining;
    }
    if (step > 0)
    {
        boundary = position->maximum_phase;
        remaining = boundary - position->phase;
        if (remaining < step)
            step = (remaining > 0) ? (int32_t)remaining : 0;
    }
    else if (step < 0)
    {
        boundary = position->minimum_phase;
        remaining = boundary - position->phase;
        if (remaining > step)
            step = (remaining < 0) ? (int32_t)remaining : 0;
    }
    return step;
}

/* Called ONLY after MotorFunc/PhaseGet, with the observed Iangle delta.
 * The 32-bit fraction has a bounded numerator: 16000*40000 + 15360000.
 * No 64-bit division or per-pulse loop runs in this 16 kHz accounting path. */
static uint8_t EcatPulse_Advance(TEcatPulsePosition *position, int32_t step)
{
    int64_t next_phase = position->phase + step;
    int64_t whole;
    int32_t scaled;
    int32_t carry;
    if ((step > ECAT_PHASE_MAX_INCREMENT) ||
        (step < -ECAT_PHASE_MAX_INCREMENT) ||
        (next_phase > position->maximum_phase) ||
        (next_phase < position->minimum_phase))
        return 0U;
    scaled = step * position->resolution + position->fraction;
    carry = scaled / ECAT_PHASE_UNITS_PER_REV;
    whole = (int64_t)position->whole_pulses + carry;
    if ((whole > INT32_MAX) || (whole < INT32_MIN))
        return 0U;
    position->phase = next_phase;
    position->whole_pulses = (int32_t)whole;
    position->fraction = scaled % ECAT_PHASE_UNITS_PER_REV;
    return 1U;
}

#endif
