#include "bus_scope_output.h"

#include <stddef.h>
#include <string.h>

void BusScope_OutputDefault(BusScopeOutputConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->frequency_hz = 10U;
    config->duty_permille = 500U;
    config->enabled = 1U;
}

uint8_t BusScope_OutputValidate(const BusScopeOutputConfig *config)
{
    return config != NULL &&
           config->frequency_hz >= 1U &&
           config->frequency_hz <= OUTPUT_PWM_MAX_HZ &&
           config->duty_permille <= 1000U && config->enabled <= 1U;
}

void BusScope_OutputAdjust(BusScopeOutputConfig *config, BusScopeOutputField field, int direction)
{
    uint32_t step;
    if (direction == 0 || !BusScope_OutputValidate(config)) return;
    switch (field)
    {
    case OUTPUT_FIELD_FREQ:
        step = config->frequency_hz - (direction < 0 ? 1U : 0U);
        step = step < 10U ? 1U : step < 100U ? 10U : step < 1000U ? 100U : step < 10000U ? 1000U : 10000U;
        config->frequency_hz = direction > 0 ?
            (config->frequency_hz + step > OUTPUT_PWM_MAX_HZ ? OUTPUT_PWM_MAX_HZ : config->frequency_hz + step) :
            (config->frequency_hz <= step ? 1U : config->frequency_hz - step);
        break;
    case OUTPUT_FIELD_DUTY:
        config->duty_permille = (uint16_t)(direction > 0 ?
            (config->duty_permille >= 990U ? 1000U : config->duty_permille + 10U) :
            (config->duty_permille <= 10U ? 0U : config->duty_permille - 10U));
        break;
    case OUTPUT_FIELD_ENABLE:
        config->enabled = (uint8_t)(direction > 0);
        break;
    default: break;
    }
}

uint8_t BusScope_OutputTiming(uint32_t clock_hz, uint32_t rate_hz,
                            uint16_t duty_permille, BusScopeOutputTiming *timing)
{
    uint64_t ticks, divider, period;
    if (timing == NULL || clock_hz == 0U || rate_hz == 0U ||
        rate_hz > clock_hz || duty_permille > 1000U) return 0U;
    ticks = ((uint64_t)clock_hz + rate_hz / 2U) / rate_hz;
    /* Reserve CCR=ARR+1 for 100% duty even on the 16-bit TIM1. */
    divider = (ticks + 65534U) / 65535U;
    if (divider == 0U || divider > 65536U) return 0U;
    period = ((uint64_t)clock_hz + (uint64_t)rate_hz * divider / 2U) / ((uint64_t)rate_hz * divider);
    if (period == 0U || period > 65535U) return 0U;
    timing->prescaler = (uint16_t)(divider - 1U);
    timing->autoreload = (uint16_t)(period - 1U);
    timing->compare = (uint32_t)((period * duty_permille + 500U) / 1000U);
    timing->actual_millihz = (uint32_t)(((uint64_t)clock_hz * 1000U + divider * period / 2U) / (divider * period));
    return 1U;
}
