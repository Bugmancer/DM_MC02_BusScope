#include "bus_scope_capture.h"

#include <string.h>

#define CAPTURE_ADC_MAX 65535U
#define CAPTURE_REFERENCE_MV 3300U
#define CAPTURE_HYSTERESIS_MV 25U
#define CAPTURE_UNKNOWN 2U

static uint16_t capture_to_mv(uint32_t raw)
{
    return (uint16_t)(((uint64_t)raw * CAPTURE_REFERENCE_MV +
                      CAPTURE_ADC_MAX / 2U) / CAPTURE_ADC_MAX);
}

static uint16_t capture_to_raw(uint32_t mv)
{
    return (uint16_t)((mv * CAPTURE_ADC_MAX + CAPTURE_REFERENCE_MV / 2U) /
                      CAPTURE_REFERENCE_MV);
}

static uint32_t capture_sqrt_round(uint64_t value)
{
    uint64_t root = 0U;
    uint64_t bit = (uint64_t)1U << 62U;

    while (bit > value) bit >>= 2U;
    while (bit != 0U)
    {
        if (value >= root + bit)
        {
            value -= root + bit;
            root = (root >> 1U) + bit;
        }
        else root >>= 1U;
        bit >>= 2U;
    }
    return (uint32_t)(root + ((value > root) ? 1U : 0U));
}

static void capture_bin_clear(BusScopeCaptureBin *bin)
{
    memset(bin, 0, sizeof(*bin));
    bin->min = UINT16_MAX;
    bin->min_period = UINT32_MAX;
}

static void capture_reset_history(BusScopeCapture *capture)
{
    capture_bin_clear(&capture->partial);
    capture->tick = 0U;
    capture->last_rise = 0U;
    capture->high_width = 0U;
    capture->auto_samples = 0U;
    capture->write_index = 0U;
    capture->history_count = 0U;
    capture->partial_count = 0U;
    capture->post_bins = 0U;
    capture->schmitt_state = CAPTURE_UNKNOWN;
    capture->cycle_started = 0U;
    capture->cycle_fell = 0U;
    capture->waiting = (capture->stopped == 0U) ? 1U : 0U;
}

void BusScopeCapture_Init(BusScopeCapture *capture)
{
    const BusScopeCaptureConfig config = {5U, 1650U, 0U, BUS_SCOPE_CAPTURE_AUTO};

    if (capture == NULL) return;
    memset(capture, 0, sizeof(*capture));
    BusScopeCapture_Configure(capture, &config);
}

void BusScopeCapture_Configure(BusScopeCapture *capture,
                               const BusScopeCaptureConfig *config)
{
    BusScopeCaptureConfig normalized;
    uint32_t low_mv;
    uint32_t high_mv;

    if (capture == NULL || config == NULL) return;
    normalized = *config;
    switch (normalized.decimation)
    {
        case 1U: case 2U: case 5U: case 10U:
        case 20U: case 50U: case 100U: case 200U: break;
        default: normalized.decimation = 5U; break;
    }
    if (normalized.trigger_mv > CAPTURE_REFERENCE_MV)
        normalized.trigger_mv = CAPTURE_REFERENCE_MV;
    normalized.falling = (normalized.falling != 0U) ? 1U : 0U;
    if ((unsigned)normalized.mode > (unsigned)BUS_SCOPE_CAPTURE_HOLD)
        normalized.mode = BUS_SCOPE_CAPTURE_AUTO;
    capture->config = normalized;
    capture->stopped = (normalized.mode == BUS_SCOPE_CAPTURE_HOLD) ? 1U : 0U;
    if (normalized.mode != BUS_SCOPE_CAPTURE_HOLD) capture->frame_ready = 0U;
    low_mv = (normalized.trigger_mv > CAPTURE_HYSTERESIS_MV) ?
             normalized.trigger_mv - CAPTURE_HYSTERESIS_MV : 0U;
    high_mv = normalized.trigger_mv + CAPTURE_HYSTERESIS_MV;
    if (high_mv > CAPTURE_REFERENCE_MV) high_mv = CAPTURE_REFERENCE_MV;
    capture->threshold_low = capture_to_raw(low_mv);
    capture->threshold_high = capture_to_raw(high_mv);
    capture_reset_history(capture);
}

void BusScopeCapture_Gap(BusScopeCapture *capture)
{
    if (capture != NULL) capture_reset_history(capture);
}

static void capture_cycle(BusScopeCapture *capture, uint32_t period,
                          uint32_t high)
{
    BusScopeCaptureBin *bin = &capture->partial;
    uint8_t bad = (period < 10U || high < 2U || period - high < 2U) ? 1U : 0U;

    /* A cycle longer than the full record cannot contribute to a measurement. */
    if (period > BUS_SCOPE_CAPTURE_SAMPLES * capture->config.decimation) return;
    if (bin->first_period == 0U)
    {
        bin->first_period = period;
        bin->first_high = high;
        bin->first_end_offset = capture->partial_count;
        bin->first_bad = bad;
    }
    else if (bad != 0U) bin->bad_cycles++;
    else
    {
        bin->period_sum += period;
        bin->high_sum += high;
        bin->cycles++;
        if (period < bin->min_period) bin->min_period = period;
        if (period > bin->max_period) bin->max_period = period;
    }
}

static uint8_t capture_edge(BusScopeCapture *capture, uint16_t raw)
{
    uint8_t state = capture->schmitt_state;
    uint8_t edge = 0U;

    if (state == CAPTURE_UNKNOWN)
    {
        if (raw <= capture->threshold_low) capture->schmitt_state = 0U;
        else if (raw >= capture->threshold_high) capture->schmitt_state = 1U;
        return 0U;
    }
    if (state == 0U && raw >= capture->threshold_high)
    {
        capture->schmitt_state = 1U;
        edge = 1U;
        if (capture->cycle_started != 0U && capture->cycle_fell != 0U)
            capture_cycle(capture, capture->tick - capture->last_rise,
                          capture->high_width);
        capture->last_rise = capture->tick;
        capture->cycle_started = 1U;
        capture->cycle_fell = 0U;
    }
    else if (state == 1U && raw <= capture->threshold_low)
    {
        capture->schmitt_state = 0U;
        edge = 2U;
        if (capture->cycle_started != 0U)
        {
            capture->high_width = capture->tick - capture->last_rise;
            capture->cycle_fell = 1U;
        }
    }
    return edge;
}

static void capture_publish(BusScopeCapture *capture, uint8_t triggered)
{
    BusScopeCaptureFrame *frame = &capture->latest;
    uint64_t sum = 0U;
    uint64_t sum_squares = 0U;
    uint64_t period_sum = 0U;
    uint64_t high_sum = 0U;
    uint32_t min_period = UINT32_MAX;
    uint32_t max_period = 0U;
    uint32_t cycles = 0U;
    uint32_t bad_cycles = 0U;
    uint32_t sample_count = BUS_SCOPE_CAPTURE_SAMPLES * capture->config.decimation;
    uint16_t raw_min = UINT16_MAX;
    uint16_t raw_max = 0U;
    uint16_t index = capture->write_index;
    uint16_t i;

    memset(&frame->stats, 0, sizeof(frame->stats));
    for (i = 0U; i < BUS_SCOPE_CAPTURE_SAMPLES; i++)
    {
        const BusScopeCaptureBin *bin = &capture->bins[index];
        uint32_t end_offset = (uint32_t)i * capture->config.decimation +
                              bin->first_end_offset;

        frame->samples[i] = (uint16_t)((bin->sum + capture->config.decimation / 2U) /
                                     capture->config.decimation);
        frame->min[i] = bin->min;
        frame->max[i] = bin->max;
        if (bin->min < raw_min) raw_min = bin->min;
        if (bin->max > raw_max) raw_max = bin->max;
        sum += bin->sum;
        sum_squares += bin->sum_squares;
        period_sum += bin->period_sum;
        high_sum += bin->high_sum;
        cycles += bin->cycles;
        bad_cycles += bin->bad_cycles;
        if (bin->min_period < min_period) min_period = bin->min_period;
        if (bin->max_period > max_period) max_period = bin->max_period;
        /* The first cycle in a bin may start before the frame's left edge. */
        if (bin->first_period != 0U && bin->first_period <= end_offset)
        {
            if (bin->first_bad != 0U) bad_cycles++;
            else
            {
                period_sum += bin->first_period;
                high_sum += bin->first_high;
                cycles++;
                if (bin->first_period < min_period) min_period = bin->first_period;
                if (bin->first_period > max_period) max_period = bin->first_period;
            }
        }
        index++;
        if (index == BUS_SCOPE_CAPTURE_SAMPLES) index = 0U;
    }
    frame->period_us = capture->config.decimation * BUS_SCOPE_CAPTURE_RAW_PERIOD_US;
    frame->sequence = ++capture->sequence;
    frame->trigger_index = BUS_SCOPE_CAPTURE_PRETRIGGER;
    frame->triggered = triggered;
    frame->stats.min_mv = capture_to_mv(raw_min);
    frame->stats.max_mv = capture_to_mv(raw_max);
    frame->stats.vpp_mv = capture_to_mv((uint32_t)raw_max - raw_min);
    frame->stats.mean_mv = (uint16_t)((sum * CAPTURE_REFERENCE_MV +
        (uint64_t)sample_count * CAPTURE_ADC_MAX / 2U) /
        ((uint64_t)sample_count * CAPTURE_ADC_MAX));
    frame->stats.rms_mv = (uint16_t)capture_sqrt_round(
        (sum_squares * CAPTURE_REFERENCE_MV / sample_count) * CAPTURE_REFERENCE_MV /
        ((uint64_t)CAPTURE_ADC_MAX * CAPTURE_ADC_MAX));
    frame->stats.undersampled = (bad_cycles != 0U) ? 1U : 0U;
    if (cycles >= 2U && bad_cycles == 0U &&
        (uint64_t)max_period * 4U <= (uint64_t)min_period * 5U)
    {
        frame->stats.freq_millihz = (uint32_t)(((uint64_t)cycles * 100000000U +
                                                period_sum / 2U) / period_sum);
        frame->stats.duty_permille = (uint16_t)((high_sum * 1000U + period_sum / 2U) /
                                                period_sum);
        frame->stats.frequency_valid = 1U;
    }
    if (capture->frame_ready != 0U) capture->frames_dropped++;
    capture->frame_ready = 1U;
    capture->auto_samples = 0U;
    if (capture->config.mode == BUS_SCOPE_CAPTURE_SINGLE)
    {
        capture->stopped = 1U;
        capture->waiting = 0U;
    }
    else capture->waiting = 1U;
}

void BusScopeCapture_Feed(BusScopeCapture *capture, const uint16_t *samples,
                         size_t count)
{
    size_t i;

    if (capture == NULL || samples == NULL || capture->stopped != 0U) return;
    for (i = 0U; i < count && capture->stopped == 0U; i++)
    {
        uint16_t raw = samples[i];
        uint8_t edge = capture_edge(capture, raw);
        BusScopeCaptureBin *bin = &capture->partial;

        if (edge == ((capture->config.falling != 0U) ? 2U : 1U) &&
            capture->history_count >= BUS_SCOPE_CAPTURE_PRETRIGGER &&
            capture->post_bins == 0U)
        {
            capture->post_bins = BUS_SCOPE_CAPTURE_SAMPLES - BUS_SCOPE_CAPTURE_PRETRIGGER;
            capture->waiting = 0U;
        }
        if (raw < bin->min) bin->min = raw;
        if (raw > bin->max) bin->max = raw;
        bin->sum += raw;
        bin->sum_squares += (uint64_t)raw * raw;
        capture->partial_count++;
        capture->tick++;
        if (capture->auto_samples < BUS_SCOPE_CAPTURE_SAMPLES *
            (uint32_t)capture->config.decimation * 2U) capture->auto_samples++;
        if (capture->partial_count == capture->config.decimation)
        {
            capture->bins[capture->write_index] = *bin;
            capture->write_index++;
            if (capture->write_index == BUS_SCOPE_CAPTURE_SAMPLES) capture->write_index = 0U;
            if (capture->history_count < BUS_SCOPE_CAPTURE_SAMPLES) capture->history_count++;
            capture_bin_clear(bin);
            capture->partial_count = 0U;
            if (capture->post_bins != 0U)
            {
                capture->post_bins--;
                if (capture->post_bins == 0U) capture_publish(capture, 1U);
            }
            else if (capture->config.mode == BUS_SCOPE_CAPTURE_AUTO &&
                     capture->auto_samples >= BUS_SCOPE_CAPTURE_SAMPLES *
                     (uint32_t)capture->config.decimation * 2U)
                capture_publish(capture, 0U);
        }
    }
}

uint8_t BusScopeCapture_TakeFrame(BusScopeCapture *capture,
                                BusScopeCaptureFrame *frame)
{
    if (capture == NULL || frame == NULL || capture->frame_ready == 0U) return 0U;
    *frame = capture->latest;
    capture->frame_ready = 0U;
    return 1U;
}
