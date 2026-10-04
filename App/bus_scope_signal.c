#include "bus_scope_signal.h"

#include <stddef.h>
#include <string.h>

#define ADC_VREF_MV       3300U
#define ADC_FULL_SCALE    65535U
#define PWM_MIN_SPAN_RAW  2000U

uint32_t BusScope_AdcToMv(uint16_t raw)
{
    return ((uint32_t)raw * ADC_VREF_MV) / ADC_FULL_SCALE;
}

void BusScope_MeasurePwm(const uint16_t *samples, uint16_t count,
                         uint32_t sample_period_us, BusScopePwmMeasure *result)
{
    uint16_t min_raw = UINT16_MAX;
    uint16_t max_raw = 0U;
    uint32_t low_threshold;
    uint32_t high_threshold;
    uint32_t last_rise = 0U;
    uint32_t cycle_high = 0U;
    uint32_t high_sum = 0U;
    uint32_t period_sum = 0U;
    uint32_t period_count = 0U;
    uint8_t state_known = 0U;
    uint8_t state_high = 0U;
    uint8_t have_rise = 0U;
    uint64_t elapsed_us;

    if (result == NULL)
    {
        return;
    }
    memset(result, 0, sizeof(*result));
    if (samples == NULL || count == 0U || sample_period_us == 0U)
    {
        return;
    }

    for (uint32_t i = 0U; i < count; i++)
    {
        if (samples[i] < min_raw) min_raw = samples[i];
        if (samples[i] > max_raw) max_raw = samples[i];
    }
    result->high_mv = BusScope_AdcToMv(max_raw);
    result->low_mv = BusScope_AdcToMv(min_raw);
    if ((uint32_t)max_raw - min_raw < PWM_MIN_SPAN_RAW)
    {
        return;
    }

    low_threshold = min_raw + ((uint32_t)max_raw - min_raw) / 3U;
    high_threshold = min_raw + (((uint32_t)max_raw - min_raw) * 2U) / 3U;

    for (uint32_t i = 0U; i < count; i++)
    {
        if (have_rise != 0U && state_high != 0U)
        {
            cycle_high++;
        }

        /* Mid-band samples retain the last known state, avoiding false edges. */
        if (samples[i] <= low_threshold)
        {
            state_high = 0U;
            state_known = 1U;
        }
        else if (samples[i] >= high_threshold)
        {
            if (state_known != 0U && state_high == 0U)
            {
                if (have_rise != 0U)
                {
                    period_sum += i - last_rise;
                    high_sum += cycle_high;
                    period_count++;
                }
                last_rise = i;
                cycle_high = 0U;
                have_rise = 1U;
            }
            state_high = 1U;
            state_known = 1U;
        }
    }

    if (period_count == 0U)
    {
        return;
    }

    elapsed_us = (uint64_t)period_sum * sample_period_us;
    result->freq_hz = (uint32_t)(((uint64_t)period_count * 1000000U +
                                  elapsed_us / 2U) / elapsed_us);
    result->duty_permille = (high_sum * 1000U + period_sum / 2U) / period_sum;
    result->valid = 1U;
}
