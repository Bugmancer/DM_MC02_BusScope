#include "bus_scope_capture.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

static BusScopeCapture capture;
static BusScopeCaptureFrame frame;

static void configure(uint16_t decimation, BusScopeCaptureMode mode, uint8_t falling)
{
    BusScopeCaptureConfig config = {decimation, 1650U, falling, mode};
    BusScopeCapture_Init(&capture);
    BusScopeCapture_Configure(&capture, &config);
}

static void feed_constant(uint16_t raw, uint32_t count)
{
    uint16_t block[127];
    size_t i;
    for (i = 0U; i < ARRAY_COUNT(block); i++) block[i] = raw;
    while (count != 0U)
    {
        size_t take = count < ARRAY_COUNT(block) ? count : ARRAY_COUNT(block);
        BusScopeCapture_Feed(&capture, block, take);
        count -= (uint32_t)take;
    }
}

static void feed_pwm(uint32_t count, uint32_t period, uint32_t high,
                     uint32_t phase, uint16_t low_raw, uint16_t high_raw)
{
    uint16_t block[127];
    uint32_t offset = 0U;
    while (offset < count)
    {
        size_t i;
        size_t take = count - offset < ARRAY_COUNT(block) ?
                      count - offset : ARRAY_COUNT(block);
        for (i = 0U; i < take; i++)
            block[i] = ((offset + (uint32_t)i + phase) % period < high) ?
                       high_raw : low_raw;
        BusScopeCapture_Feed(&capture, block, take);
        offset += (uint32_t)take;
    }
}

static uint32_t frame_samples(void)
{
    return BUS_SCOPE_CAPTURE_SAMPLES * (uint32_t)capture.config.decimation;
}

static void test_config_and_null(void)
{
    BusScopeCaptureConfig config = {3U, UINT16_MAX, 9U, (BusScopeCaptureMode)99};
    assert(sizeof(capture) < 20000U);
    BusScopeCapture_Init(NULL);
    BusScopeCapture_Init(&capture);
    assert(capture.config.decimation == 5U);
    assert(capture.config.trigger_mv == 1650U);
    assert(capture.config.mode == BUS_SCOPE_CAPTURE_AUTO);
    assert(capture.waiting != 0U && capture.stopped == 0U);
    BusScopeCapture_Configure(&capture, &config);
    assert(capture.config.decimation == 5U);
    assert(capture.config.trigger_mv == 3300U);
    assert(capture.config.falling == 1U);
    assert(capture.config.mode == BUS_SCOPE_CAPTURE_AUTO);
    assert(capture.threshold_low < capture.threshold_high);
    config.trigger_mv = 0U;
    BusScopeCapture_Configure(&capture, &config);
    assert(capture.threshold_low == 0U && capture.threshold_high > 0U);
    BusScopeCapture_Configure(NULL, &config);
    BusScopeCapture_Configure(&capture, NULL);
    BusScopeCapture_Feed(NULL, NULL, 0U);
    BusScopeCapture_Feed(&capture, NULL, 10U);
    BusScopeCapture_Gap(NULL);
    assert(!BusScopeCapture_TakeFrame(NULL, &frame));
    assert(!BusScopeCapture_TakeFrame(&capture, NULL));
    assert(!BusScopeCapture_TakeFrame(&capture, &frame));
}

static void test_trigger_positions_and_phase(void)
{
    const uint16_t decimations[] = {1U, 2U, 5U, 10U, 20U, 50U, 100U, 200U};
    size_t d;
    for (d = 0U; d < ARRAY_COUNT(decimations); d++)
    {
        uint16_t phase;
        for (phase = 0U; phase < decimations[d]; phase++)
        {
            uint8_t falling;
            for (falling = 0U; falling < 2U; falling++)
            {
                uint16_t before = falling != 0U ? UINT16_MAX : 0U;
                uint16_t after = falling != 0U ? 0U : UINT16_MAX;
                uint32_t pre = (BUS_SCOPE_CAPTURE_PRETRIGGER + 11U) *
                               decimations[d] + phase;
                uint32_t post = (BUS_SCOPE_CAPTURE_SAMPLES -
                                BUS_SCOPE_CAPTURE_PRETRIGGER) * decimations[d] - phase;
                configure(decimations[d], BUS_SCOPE_CAPTURE_NORMAL, falling);
                feed_constant(before, pre);
                assert(!BusScopeCapture_TakeFrame(&capture, &frame));
                feed_constant(after, post - 1U);
                assert(capture.waiting == 0U);
                assert(!BusScopeCapture_TakeFrame(&capture, &frame));
                feed_constant(after, 1U);
                assert(BusScopeCapture_TakeFrame(&capture, &frame));
                assert(frame.triggered && frame.trigger_index == 68U);
                assert(frame.period_us == (uint32_t)decimations[d] * 10U);
                assert(frame.samples[67] == before);
                assert(frame.samples[69] == after);
                assert(frame.samples[271] == after);
                assert((falling ? frame.min[68] : frame.max[68]) == after);
                assert(!frame.stats.frequency_valid);
                assert(capture.waiting != 0U);
            }
        }
    }
}

static void test_auto_normal_and_hysteresis(void)
{
    configure(5U, BUS_SCOPE_CAPTURE_AUTO, 0U);
    feed_constant(32768U, frame_samples() * 2U - 1U);
    assert(!BusScopeCapture_TakeFrame(&capture, &frame));
    feed_constant(32768U, 1U);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(!frame.triggered && frame.stats.mean_mv == 1650U);
    assert(frame.stats.rms_mv == 1650U && frame.stats.vpp_mv == 0U);
    assert(!frame.stats.frequency_valid && !frame.stats.undersampled);
    feed_constant(32768U, frame_samples() * 4U);
    assert(capture.frames_dropped == 1U);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(frame.sequence == 3U);

    configure(5U, BUS_SCOPE_CAPTURE_NORMAL, 0U);
    feed_pwm(frame_samples() * 5U, 20U, 10U, 0U, 32500U, 33000U);
    assert(!BusScopeCapture_TakeFrame(&capture, &frame));
    assert(capture.waiting != 0U);
}

static void test_single_hold_and_rearm(void)
{
    BusScopeCaptureConfig config;
    uint32_t sequence;
    configure(5U, BUS_SCOPE_CAPTURE_SINGLE, 0U);
    feed_pwm(frame_samples() * 4U, 100U, 25U, 0U, 0U, UINT16_MAX);
    assert(capture.stopped && capture.config.mode == BUS_SCOPE_CAPTURE_SINGLE);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    sequence = frame.sequence;
    feed_pwm(frame_samples() * 4U, 100U, 25U, 0U, 0U, UINT16_MAX);
    assert(!BusScopeCapture_TakeFrame(&capture, &frame));
    BusScopeCapture_Gap(&capture);
    assert(capture.stopped && !capture.waiting);
    config = capture.config;
    BusScopeCapture_Configure(&capture, &config);
    assert(!capture.stopped && capture.waiting);
    feed_pwm(frame_samples() * 4U, 100U, 25U, 0U, 0U, UINT16_MAX);
    assert(capture.stopped);
    config.mode = BUS_SCOPE_CAPTURE_HOLD;
    BusScopeCapture_Configure(&capture, &config);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(frame.sequence == sequence + 1U);
    sequence = frame.sequence;
    feed_constant(0U, frame_samples() * 5U);
    assert(!BusScopeCapture_TakeFrame(&capture, &frame));
    assert(capture.stopped && capture.sequence == sequence);
}

static void test_raw_measurements_and_envelope(void)
{
    const uint16_t decimations[] = {1U, 2U, 5U, 10U, 20U, 50U, 100U, 200U};
    size_t d;
    for (d = 0U; d < ARRAY_COUNT(decimations); d++)
    {
        uint16_t i;
        uint8_t found_peak = 0U;
        configure(decimations[d], BUS_SCOPE_CAPTURE_SINGLE, 0U);
        feed_pwm(frame_samples() * 4U, 50U, 2U, 17U, 0U, UINT16_MAX);
        assert(BusScopeCapture_TakeFrame(&capture, &frame));
        assert(frame.stats.frequency_valid && !frame.stats.undersampled);
        assert(frame.stats.freq_millihz == 2000000U);
        assert(frame.stats.duty_permille == 40U);
        assert(frame.stats.min_mv == 0U && frame.stats.max_mv == 3300U);
        for (i = 0U; i < BUS_SCOPE_CAPTURE_SAMPLES; i++)
        {
            assert(frame.min[i] <= frame.samples[i]);
            assert(frame.samples[i] <= frame.max[i]);
            if (frame.max[i] == UINT16_MAX) found_peak = 1U;
        }
        assert(found_peak);
    }

    configure(100U, BUS_SCOPE_CAPTURE_SINGLE, 0U);
    feed_pwm(frame_samples() * 4U, 100U, 25U, 0U, 0U, UINT16_MAX);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(frame.stats.frequency_valid && frame.stats.freq_millihz == 1000000U);
    assert(frame.stats.duty_permille == 250U);
    assert(frame.stats.mean_mv == 825U && frame.stats.rms_mv == 1650U);
    assert(frame.samples[0] == 16384U);
    assert(frame.min[0] == 0U && frame.max[0] == UINT16_MAX);

    configure(200U, BUS_SCOPE_CAPTURE_AUTO, 0U);
    feed_constant(UINT16_MAX, frame_samples() * 2U);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(frame.stats.mean_mv == 3300U && frame.stats.rms_mv == 3300U);
    assert(frame.stats.min_mv == 3300U && frame.stats.max_mv == 3300U);
    assert(frame.stats.vpp_mv == 0U && !frame.stats.frequency_valid);
}

static void test_reject_undersampled_and_incomplete(void)
{
    const uint16_t periods[] = {2U, 8U, 50U, 50U};
    const uint16_t highs[] = {1U, 4U, 1U, 49U};
    size_t i;
    for (i = 0U; i < ARRAY_COUNT(periods); i++)
    {
        configure(200U, BUS_SCOPE_CAPTURE_SINGLE, 0U);
        feed_pwm(frame_samples() * 4U, periods[i], highs[i], 0U, 0U, UINT16_MAX);
        assert(BusScopeCapture_TakeFrame(&capture, &frame));
        assert(!frame.stats.frequency_valid && frame.stats.undersampled);
        assert(frame.stats.freq_millihz == 0U && frame.stats.duty_permille == 0U);
    }

    configure(1U, BUS_SCOPE_CAPTURE_SINGLE, 0U);
    feed_pwm(frame_samples() * 4U, 150U, 50U, 1U, 0U, UINT16_MAX);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(!frame.stats.frequency_valid && !frame.stats.undersampled);

    configure(10U, BUS_SCOPE_CAPTURE_SINGLE, 0U);
    for (i = 0U; i < 100U; i++)
    {
        feed_constant(0U, (i % 2U) ? 10U : 50U);
        feed_constant(UINT16_MAX, 10U);
    }
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(!frame.stats.frequency_valid);
}

static void test_gap_and_config_discard_partial(void)
{
    BusScopeCaptureConfig config;
    configure(5U, BUS_SCOPE_CAPTURE_NORMAL, 0U);
    feed_constant(0U, 500U);
    feed_constant(UINT16_MAX, 200U);
    assert(capture.post_bins != 0U);
    BusScopeCapture_Gap(&capture);
    assert(!capture.post_bins && !capture.history_count && !capture.partial_count);
    feed_constant(UINT16_MAX, frame_samples() * 3U);
    assert(!BusScopeCapture_TakeFrame(&capture, &frame));
    feed_pwm(frame_samples() * 3U, 50U, 10U, 30U, 0U, UINT16_MAX);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(frame.stats.frequency_valid && frame.stats.freq_millihz == 2000000U);
    config = capture.config;
    config.decimation = 200U;
    BusScopeCapture_Configure(&capture, &config);
    assert(!capture.history_count && !capture.frame_ready);
    feed_constant(UINT16_MAX, frame_samples() * 3U);
    assert(!BusScopeCapture_TakeFrame(&capture, &frame));
}

static void test_tick_wrap(void)
{
    configure(20U, BUS_SCOPE_CAPTURE_SINGLE, 0U);
    capture.tick = UINT32_MAX - 1000U;
    feed_pwm(frame_samples() * 3U, 100U, 25U, 19U, 0U, UINT16_MAX);
    assert(BusScopeCapture_TakeFrame(&capture, &frame));
    assert(frame.stats.frequency_valid && frame.stats.freq_millihz == 1000000U);
    assert(frame.stats.duty_permille == 250U);
}

int main(void)
{
    test_config_and_null();
    test_trigger_positions_and_phase();
    test_auto_normal_and_hysteresis();
    test_single_hold_and_rearm();
    test_raw_measurements_and_envelope();
    test_reject_undersampled_and_incomplete();
    test_gap_and_config_discard_partial();
    test_tick_wrap();
    printf("bus_scope_capture: all tests passed (context %lu bytes)\n",
           (unsigned long)sizeof(capture));
    return 0;
}
