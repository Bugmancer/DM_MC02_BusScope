#include "bus_scope_output.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct
{
    uint32_t divider;
    uint32_t period;
    uint64_t ticks;
    uint64_t error;
} OracleTiming;

static OracleTiming exhaustive_timing(uint32_t clock_hz, uint32_t rate_hz)
{
    OracleTiming best = {0U, 0U, 0U, 0U};
    /* Independent exhaustive enumeration by ARR, without factor symmetry,
       search bounds, or exact-solution early exits used by the firmware. */
    for (uint32_t period = 1U; period <= 65535U; period++)
    {
        uint32_t floor_divider = (uint32_t)(clock_hz / ((uint64_t)rate_hz * period));
        for (uint32_t offset = 0U; offset <= 1U; offset++)
        {
            uint32_t divider = floor_divider + offset;
            uint64_t ticks, target, error;
            if (divider == 0U) divider = 1U;
            if (divider > 65536U) divider = 65536U;
            ticks = (uint64_t)period * divider;
            target = (uint64_t)rate_hz * ticks;
            error = target >= clock_hz ? target - clock_hz : clock_hz - target;
            if (best.ticks == 0U || error * best.ticks < best.error * ticks ||
                (error * best.ticks == best.error * ticks && period > best.period))
            {
                best.divider = divider;
                best.period = period;
                best.ticks = ticks;
                best.error = error;
            }
        }
    }
    return best;
}

static void assert_optimal_timing(uint32_t clock_hz, uint32_t rate_hz)
{
    BusScopeOutputTiming t;
    OracleTiming expected = exhaustive_timing(clock_hz, rate_hz);
    uint64_t ticks;
    assert(BusScope_OutputTiming(clock_hz, rate_hz, 500U, &t));
    ticks = ((uint64_t)t.prescaler + 1U) * ((uint64_t)t.autoreload + 1U);
    assert((uint32_t)t.autoreload + 1U == expected.period);
    assert((uint32_t)t.prescaler + 1U == expected.divider);
    assert(t.actual_millihz == ((uint64_t)clock_hz * 1000U + ticks / 2U) / ticks);
    assert(t.compare == ((uint32_t)t.autoreload + 2U) / 2U);
}

static void test_optimal_timing(void)
{
    static const uint32_t clocks[] = {240000000U, 550000000U, 479999983U};
    static const uint32_t frequencies[] = {
        1U, 2U, 3U, 7U, 10U, 37U, 100U, 123U, 997U, 1000U,
        1234U, 7919U, 10000U, 32767U, 99991U, 100000U
    };
    BusScopeOutputTiming t;
    uint32_t random = 0x2d71ca93U;
    for (size_t i = 0U; i < sizeof(clocks) / sizeof(clocks[0]); i++)
        for (size_t j = 0U; j < sizeof(frequencies) / sizeof(frequencies[0]); j++)
            assert_optimal_timing(clocks[i], frequencies[j]);
    for (unsigned i = 0U; i < 128U; i++)
    {
        uint32_t clock_hz, rate_hz;
        random = random * 1664525U + 1013904223U;
        clock_hz = random % 550000000U + 1U;
        random = random * 1664525U + 1013904223U;
        rate_hz = random % (i % 2U == 0U ? 100000U : 100U) + 1U;
        if (rate_hz > clock_hz) rate_hz = clock_hz;
        assert_optimal_timing(clock_hz, rate_hz);
    }
    /* Reciprocal-frequency error differs from rounding clock / rate. */
    assert_optimal_timing(17U, 7U);
    assert(BusScope_OutputTiming(17U, 7U, 500U, &t));
    assert(t.prescaler == 0U && t.autoreload == 2U);
    /* Equal absolute Hz error chooses the longer period (4 Hz or 2 Hz). */
    assert_optimal_timing(4U, 3U);
    assert(BusScope_OutputTiming(4U, 3U, 500U, &t));
    assert(t.prescaler == 0U && t.autoreload == 1U);
    assert_optimal_timing(1U, 1U);
    assert_optimal_timing(65535U, 1U);
    assert_optimal_timing(65536U, 1U);
    /* An exact 1 Hz solution exists; the previous first-fit divider missed it. */
    assert(BusScope_OutputTiming(240000000U, 1U, 500U, &t));
    assert(t.prescaler == 3749U && t.autoreload == 63999U);
}

static void test_duty_rounding(void)
{
    static const uint32_t frequencies[] = {1U, 37U, 997U, 99991U, 100000U};
    for (size_t i = 0U; i < sizeof(frequencies) / sizeof(frequencies[0]); i++)
    {
        BusScopeOutputTiming t;
        uint32_t previous = 0U;
        for (uint16_t duty = 0U; duty <= 1000U; duty++)
        {
            uint32_t period, scaled, rounded, difference;
            assert(BusScope_OutputTiming(240000000U, frequencies[i], duty, &t));
            period = (uint32_t)t.autoreload + 1U;
            assert(t.compare <= period && period <= UINT16_MAX);
            assert(t.compare >= previous);
            scaled = period * duty;
            rounded = t.compare * 1000U;
            difference = rounded >= scaled ? rounded - scaled : scaled - rounded;
            assert(difference <= 500U);
            if (duty == 0U) assert(t.compare == 0U);
            if (duty == 1000U) assert(t.compare == period);
            previous = t.compare;
        }
    }
}

static void test_invalid_timing(void)
{
    BusScopeOutputTiming t, unchanged;
    memset(&t, 0x5a, sizeof(t));
    unchanged = t;
    assert(!BusScope_OutputTiming(0U, 10U, 500U, &t));
    assert(!BusScope_OutputTiming(550000001U, 10U, 500U, &t));
    assert(!BusScope_OutputTiming(UINT32_MAX, 10U, 500U, &t));
    assert(!BusScope_OutputTiming(240000000U, 0U, 500U, &t));
    assert(!BusScope_OutputTiming(240000000U, OUTPUT_PWM_MAX_HZ + 1U, 500U, &t));
    assert(!BusScope_OutputTiming(100U, 101U, 500U, &t));
    assert(!BusScope_OutputTiming(100U, 10U, 1001U, &t));
    assert(!BusScope_OutputTiming(100U, 10U, 500U, NULL));
    assert(memcmp(&t, &unchanged, sizeof(t)) == 0);
}

static void test_adjustments(void)
{
    BusScopeOutputConfig c, unchanged;
    static const uint32_t steps[] = {1U, 10U, 100U, 1000U, 10000U};
    BusScope_OutputDefault(&c);
    assert(BusScope_OutputValidate(&c));
    assert(c.frequency_hz == 10U && c.frequency_step_hz == 10U);
    assert(c.duty_permille == 500U && c.enabled == 1U);
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_STEP, -1);
    for (size_t i = 0U; i < sizeof(steps) / sizeof(steps[0]); i++)
    {
        assert(c.frequency_step_hz == steps[i]);
        c.frequency_hz = 12345U;
        BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, 1);
        assert(c.frequency_hz == 12345U + steps[i]);
        BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, -1);
        assert(c.frequency_hz == 12345U);
        BusScope_OutputAdjust(&c, OUTPUT_FIELD_STEP, 1);
    }
    assert(c.frequency_step_hz == 10000U);
    for (unsigned i = 0U; i < 20U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, 1);
    assert(c.frequency_hz == OUTPUT_PWM_MAX_HZ);
    for (unsigned i = 0U; i < 20U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, -1);
    assert(c.frequency_hz == 1U);
    for (unsigned i = 0U; i < 8U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_STEP, -1);
    assert(c.frequency_step_hz == 1U);
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, 1);
    assert(c.frequency_hz == 2U);
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_DUTY, 1);
    assert(c.duty_permille == 501U);
    for (unsigned i = 0U; i < 1100U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_DUTY, -1);
    assert(c.duty_permille == 0U);
    for (unsigned i = 0U; i < 1100U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_DUTY, 1);
    assert(c.duty_permille == 1000U);
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_ENABLE, -1);
    assert(c.enabled == 0U);
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_ENABLE, 1);
    assert(c.enabled == 1U);
    unchanged = c;
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, 0);
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_COUNT, 1);
    assert(memcmp(&c, &unchanged, sizeof(c)) == 0);
    c.frequency_step_hz = 3U;
    assert(!BusScope_OutputValidate(&c));
    unchanged = c;
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, 1);
    assert(memcmp(&c, &unchanged, sizeof(c)) == 0);
    BusScope_OutputDefault(&c);
    c.frequency_hz = OUTPUT_PWM_MAX_HZ + 1U;
    assert(!BusScope_OutputValidate(&c));
    assert(!BusScope_OutputValidate(NULL));
    BusScope_OutputAdjust(NULL, OUTPUT_FIELD_FREQ, 1);
}

static void report_solver_cost(void)
{
    BusScopeOutputTiming timing;
    clock_t start = clock();
    for (uint32_t rate = 1U; rate <= 1000U; rate++)
        assert(BusScope_OutputTiming(549999989U, rate, 500U, &timing));
    printf("output timing: 1000 low-frequency requests in %.3f host ms\n",
           (double)(clock() - start) * 1000.0 / CLOCKS_PER_SEC);
}

int main(void)
{
    test_optimal_timing();
    test_duty_rounding();
    test_invalid_timing();
    test_adjustments();
    report_solver_cost();
    puts("bus_scope_output: all tests passed");
    return 0;
}
