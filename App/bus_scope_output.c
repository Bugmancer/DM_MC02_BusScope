#include "bus_scope_output.h"

#include <stddef.h>
#include <string.h>

#define OUTPUT_TIM_PERIOD_MAX 65535U
#define OUTPUT_TIM_DIVIDER_MAX 65536U
#define OUTPUT_TIM_CLOCK_MAX_HZ 550000000U

typedef struct
{
    uint32_t divider;
    uint32_t period;
    uint64_t ticks;
    uint64_t error;
} OutputTimingCandidate;

static uint8_t output_valid_step(uint32_t step)
{
    return step == 1U || step == 10U || step == 100U ||
           step == 1000U || step == 10000U;
}

void BusScope_OutputDefault(BusScopeOutputConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->frequency_hz = 10U;
    config->frequency_step_hz = 10U;
    config->duty_permille = 500U;
    config->enabled = 1U;
}

uint8_t BusScope_OutputValidate(const BusScopeOutputConfig *config)
{
    return config != NULL &&
           config->frequency_hz >= 1U &&
           config->frequency_hz <= OUTPUT_PWM_MAX_HZ &&
           output_valid_step(config->frequency_step_hz) &&
           config->duty_permille <= 1000U && config->enabled <= 1U;
}

void BusScope_OutputAdjust(BusScopeOutputConfig *config, BusScopeOutputField field, int direction)
{
    uint32_t step;
    if (direction == 0 || !BusScope_OutputValidate(config)) return;
    switch (field)
    {
    case OUTPUT_FIELD_FREQ:
        step = config->frequency_step_hz;
        config->frequency_hz = direction > 0 ?
            (config->frequency_hz + step > OUTPUT_PWM_MAX_HZ ? OUTPUT_PWM_MAX_HZ : config->frequency_hz + step) :
            (config->frequency_hz <= step ? 1U : config->frequency_hz - step);
        break;
    case OUTPUT_FIELD_STEP:
        if (direction > 0 && config->frequency_step_hz < 10000U)
            config->frequency_step_hz *= 10U;
        else if (direction < 0 && config->frequency_step_hz > 1U)
            config->frequency_step_hz /= 10U;
        break;
    case OUTPUT_FIELD_DUTY:
        config->duty_permille = (uint16_t)(direction > 0 ?
            (config->duty_permille >= 1000U ? 1000U : config->duty_permille + 1U) :
            (config->duty_permille <= 1U ? 0U : config->duty_permille - 1U));
        break;
    case OUTPUT_FIELD_ENABLE:
        config->enabled = (uint8_t)(direction > 0);
        break;
    default: break;
    }
}

static void output_consider_period(uint32_t clock_hz, uint32_t rate_hz,
                                   uint32_t divider, uint32_t period,
                                   OutputTimingCandidate *best)
{
    uint64_t ticks = (uint64_t)divider * period;
    uint64_t target = (uint64_t)rate_hz * ticks;
    uint64_t error = target > clock_hz ? target - clock_hz : clock_hz - target;
    uint64_t candidate_error = error * best->ticks;
    uint64_t best_error = best->error * ticks;

    /* Compare |clock / ticks - rate| exactly, before rounding the display value. */
    if (best->ticks == 0U || candidate_error < best_error ||
        (candidate_error == best_error && period > best->period))
    {
        best->divider = divider;
        best->period = period;
        best->ticks = ticks;
        best->error = error;
    }
}

static void output_consider_divider(uint32_t clock_hz, uint32_t rate_hz,
                                    uint32_t divider, OutputTimingCandidate *best)
{
    uint32_t period = (uint32_t)(clock_hz / ((uint64_t)rate_hz * divider));
    if (period == 0U)
    {
        output_consider_period(clock_hz, rate_hz, divider, 1U, best);
        return;
    }
    if (period > OUTPUT_TIM_PERIOD_MAX) period = OUTPUT_TIM_PERIOD_MAX;
    output_consider_period(clock_hz, rate_hz, divider, period, best);
    if (period < OUTPUT_TIM_PERIOD_MAX)
        output_consider_period(clock_hz, rate_hz, divider, period + 1U, best);
}

uint8_t BusScope_OutputTiming(uint32_t clock_hz, uint32_t rate_hz,
                            uint16_t duty_permille, BusScopeOutputTiming *timing)
{
    OutputTimingCandidate best = {0U, 0U, 0U, 0U};
    uint32_t divider;
    if (timing == NULL || clock_hz == 0U || clock_hz > OUTPUT_TIM_CLOCK_MAX_HZ ||
        rate_hz == 0U || rate_hz > OUTPUT_PWM_MAX_HZ ||
        rate_hz > clock_hz || duty_permille > 1000U) return 0U;

    /* Below this divider every period is too short; only the last can win. */
    divider = (uint32_t)(clock_hz / ((uint64_t)rate_hz * OUTPUT_TIM_PERIOD_MAX));
    if (divider == 0U) divider = 1U;
    for (;; divider++)
    {
        output_consider_divider(clock_hz, rate_hz, divider, &best);
        /* With PSC=0 and a fitting period, both nearest integer divisors are
           representable already; no factorization can improve the result. */
        if (divider == 1U && (uint64_t)rate_hz * OUTPUT_TIM_PERIOD_MAX >= clock_hz)
            break;
        if (best.error == 0U || (uint64_t)divider * divider * rate_hz >= clock_hz)
            break;
    }

    /* Swapping factors preserves frequency and increases period when divider >
       period. Thus sqrt(clock/rate) suffices, except divider=65536 cannot swap:
       CCR=ARR+1 must still fit in 16 bits at 100% duty. At <=550 MHz the search
       visits <=23454 dividers. Error <=6553600000 and ticks <=550065536 keep
       their cross products below 2^64 across the validated input range. */
    if (best.error != 0U && (uint64_t)rate_hz * OUTPUT_TIM_PERIOD_MAX < clock_hz)
        output_consider_divider(clock_hz, rate_hz, OUTPUT_TIM_DIVIDER_MAX, &best);

    timing->prescaler = (uint16_t)(best.divider - 1U);
    timing->autoreload = (uint16_t)(best.period - 1U);
    timing->compare = (uint32_t)((best.period * duty_permille + 500U) / 1000U);
    timing->actual_millihz = (uint32_t)(((uint64_t)clock_hz * 1000U + best.ticks / 2U) / best.ticks);
    return 1U;
}
