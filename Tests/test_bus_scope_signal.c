#include "bus_scope_signal.h"

#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_COUNT(values) ((uint16_t)(sizeof(values) / sizeof((values)[0])))

static void fill_pwm(uint16_t *samples, uint16_t count, uint16_t period,
                     uint16_t high_width, uint16_t phase,
                     uint16_t low_raw, uint16_t high_raw)
{
    for (uint32_t i = 0U; i < count; i++)
    {
        samples[i] = ((i + phase) % period < high_width) ? high_raw : low_raw;
    }
}

static void assert_zero(const BusScopePwmMeasure *result)
{
    assert(result->valid == 0U);
    assert(result->freq_hz == 0U);
    assert(result->duty_permille == 0U);
    assert(result->high_mv == 0U);
    assert(result->low_mv == 0U);
}

static void test_invalid_arguments(void)
{
    const uint16_t samples[] = {0U, UINT16_MAX};
    BusScopePwmMeasure result;

    BusScope_MeasurePwm(NULL, 0U, 0U, NULL);
    memset(&result, 0xff, sizeof(result));
    BusScope_MeasurePwm(NULL, 2U, 2000U, &result);
    assert_zero(&result);
    memset(&result, 0xff, sizeof(result));
    BusScope_MeasurePwm(samples, 0U, 2000U, &result);
    assert_zero(&result);
    memset(&result, 0xff, sizeof(result));
    BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), 0U, &result);
    assert_zero(&result);
}

static void test_ten_hz_and_partial_cycles(void)
{
    uint16_t samples[272];
    BusScopePwmMeasure result;

    /* All phases include incomplete end cycles; duty must remain invariant. */
    for (uint16_t phase = 0U; phase < 100U; phase++)
    {
        fill_pwm(samples, ARRAY_COUNT(samples), 100U, 25U, phase, 0U, UINT16_MAX);
        BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), 1000U, &result);
        assert(result.valid == 1U);
        assert(result.freq_hz == 10U);
        assert(result.duty_permille == 250U);
        assert(result.high_mv == 3300U);
        assert(result.low_mv == 0U);
    }
    for (uint16_t phase = 0U; phase < 50U; phase++)
    {
        fill_pwm(samples, ARRAY_COUNT(samples), 50U, 25U, phase, 500U, 60000U);
        BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), 2000U, &result);
        assert(result.valid == 1U);
        assert(result.freq_hz == 10U);
        assert(result.duty_permille == 500U);
    }
}

static void test_dc_and_minimum_span(void)
{
    uint16_t samples[30];
    BusScopePwmMeasure result;

    fill_pwm(samples, ARRAY_COUNT(samples), 10U, 5U, 0U, 32000U, 32000U);
    BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), 2000U, &result);
    assert(result.valid == 0U);
    assert(result.freq_hz == 0U);
    assert(result.duty_permille == 0U);
    assert(result.high_mv == BusScope_AdcToMv(32000U));
    assert(result.low_mv == result.high_mv);

    fill_pwm(samples, ARRAY_COUNT(samples), 10U, 5U, 0U, 30000U, 31999U);
    BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), 2000U, &result);
    assert(result.valid == 0U);
    assert(result.freq_hz == 0U);
    assert(result.duty_permille == 0U);

    fill_pwm(samples, ARRAY_COUNT(samples), 10U, 5U, 0U, 30000U, 32000U);
    BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), 2000U, &result);
    assert(result.valid == 1U);
    assert(result.freq_hz == 50U);
    assert(result.duty_permille == 500U);
}

static void test_insufficient_edges(void)
{
    const uint16_t high_start[] = {60000U, 60000U, 0U, 0U, 60000U};
    const uint16_t low_start[] = {0U, 0U, 60000U, 60000U, 0U};
    const uint16_t unknown_start[] = {30000U, 60000U, 0U, 60000U};
    const uint16_t one_cycle[] = {0U, 60000U, 0U, 60000U};
    BusScopePwmMeasure result;

    BusScope_MeasurePwm(high_start, ARRAY_COUNT(high_start), 2000U, &result);
    assert(result.valid == 0U);
    assert(result.duty_permille == 0U);
    BusScope_MeasurePwm(low_start, ARRAY_COUNT(low_start), 2000U, &result);
    assert(result.valid == 0U);
    BusScope_MeasurePwm(unknown_start, ARRAY_COUNT(unknown_start), 2000U, &result);
    assert(result.valid == 0U);
    BusScope_MeasurePwm(low_start, 1U, 2000U, &result);
    assert(result.valid == 0U);
    BusScope_MeasurePwm(one_cycle, ARRAY_COUNT(one_cycle), 2000U, &result);
    assert(result.valid == 1U);
    assert(result.freq_hz == 250U);
    assert(result.duty_permille == 500U);
}

static void test_hysteresis(void)
{
    const uint16_t samples[] = {
        0U, 60000U, 31000U, 29000U, 31000U, 29000U,
        0U, 31000U, 29000U, 31000U, 29000U,
        60000U, 31000U, 29000U, 31000U, 29000U,
        0U, 31000U, 29000U, 31000U, 29000U, 60000U
    };
    BusScopePwmMeasure result;

    BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), 10000U, &result);
    assert(result.valid == 1U);
    assert(result.freq_hz == 10U);
    assert(result.duty_permille == 500U);
}

static void test_integer_bounds_and_rounding(void)
{
    static uint16_t samples[UINT16_MAX];
    const uint16_t thirds[] = {0U, 60000U, 0U, 0U, 60000U};
    BusScopePwmMeasure result;

    fill_pwm(samples, ARRAY_COUNT(samples), 2U, 1U, 1U, 0U, UINT16_MAX);
    BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), 1U, &result);
    assert(result.valid == 1U);
    assert(result.freq_hz == 500000U);
    assert(result.duty_permille == 500U);

    fill_pwm(samples, ARRAY_COUNT(samples), 65000U, 32000U, 64999U, 0U, UINT16_MAX);
    BusScope_MeasurePwm(samples, ARRAY_COUNT(samples), UINT32_MAX, &result);
    assert(result.valid == 1U);
    assert(result.freq_hz == 0U);
    assert(result.duty_permille == 492U);

    BusScope_MeasurePwm(thirds, ARRAY_COUNT(thirds), 2000U, &result);
    assert(result.valid == 1U);
    assert(result.freq_hz == 167U);
    assert(result.duty_permille == 333U);

    assert(BusScope_AdcToMv(0U) == 0U);
    assert(BusScope_AdcToMv(UINT16_MAX) == 3300U);
    for (uint32_t raw = 1U; raw <= UINT16_MAX; raw++)
    {
        assert(BusScope_AdcToMv((uint16_t)raw) >=
               BusScope_AdcToMv((uint16_t)(raw - 1U)));
        assert(BusScope_AdcToMv((uint16_t)raw) <= 3300U);
    }
}

int main(void)
{
    test_invalid_arguments();
    test_ten_hz_and_partial_cycles();
    test_dc_and_minimum_span();
    test_insufficient_edges();
    test_hysteresis();
    test_integer_bounds_and_rounding();
    puts("bus_scope_signal: all tests passed");
    return 0;
}
