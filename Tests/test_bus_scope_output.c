#include "bus_scope_output.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>

static void test_timing(void)
{
    BusScopeOutputTiming t;
    static const uint32_t frequencies[] = {1U, 10U, 100U, 1000U, 10000U, 100000U};
    for (size_t i = 0U; i < sizeof(frequencies) / sizeof(frequencies[0]); i++)
    {
        uint32_t hz = frequencies[i];
        uint64_t divisor;
        assert(BusScope_OutputTiming(240000000U, hz, 500U, &t));
        divisor = ((uint64_t)t.prescaler + 1U) * ((uint64_t)t.autoreload + 1U);
        assert(t.actual_millihz == (240000000000ULL + divisor / 2U) / divisor);
        assert(t.actual_millihz >= hz * 999U && t.actual_millihz <= hz * 1001U);
        assert(t.compare >= ((uint32_t)t.autoreload + 1U) / 2U);
        assert(BusScope_OutputTiming(240000000U, hz, 0U, &t) && t.compare == 0U);
        assert(BusScope_OutputTiming(240000000U, hz, 1000U, &t));
        assert(t.compare == (uint32_t)t.autoreload + 1U && t.compare <= UINT16_MAX);
    }
    assert(!BusScope_OutputTiming(0U, 10U, 500U, &t));
    assert(!BusScope_OutputTiming(240000000U, 0U, 500U, &t));
    assert(!BusScope_OutputTiming(100U, 101U, 500U, &t));
    assert(!BusScope_OutputTiming(100U, 10U, 1001U, &t));
    assert(!BusScope_OutputTiming(100U, 10U, 500U, NULL));
}

static void test_adjustments(void)
{
    BusScopeOutputConfig c;
    BusScope_OutputDefault(&c);
    assert(BusScope_OutputValidate(&c));
    for (unsigned i = 0U; i < 1000U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, 1);
    assert(c.frequency_hz == OUTPUT_PWM_MAX_HZ);
    for (unsigned i = 0U; i < 1000U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_FREQ, -1);
    assert(c.frequency_hz == 1U);
    for (unsigned i = 0U; i < 110U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_DUTY, -1);
    assert(c.duty_permille == 0U);
    for (unsigned i = 0U; i < 110U; i++) BusScope_OutputAdjust(&c, OUTPUT_FIELD_DUTY, 1);
    assert(c.duty_permille == 1000U);
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_ENABLE, -1);
    assert(c.enabled == 0U);
    BusScope_OutputAdjust(&c, OUTPUT_FIELD_ENABLE, 1);
    assert(c.enabled == 1U);
    c.frequency_hz = OUTPUT_PWM_MAX_HZ + 1U;
    assert(!BusScope_OutputValidate(&c));
    assert(!BusScope_OutputValidate(NULL));
}

int main(void)
{
    test_timing(); test_adjustments();
    puts("bus_scope_output: all tests passed");
    return 0;
}
