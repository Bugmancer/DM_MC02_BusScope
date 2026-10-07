#ifndef BUS_SCOPE_CAPTURE_H
#define BUS_SCOPE_CAPTURE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUS_SCOPE_CAPTURE_SAMPLES 272U
#define BUS_SCOPE_CAPTURE_PRETRIGGER 68U
#define BUS_SCOPE_CAPTURE_RAW_PERIOD_US 10U

typedef enum
{
    BUS_SCOPE_CAPTURE_AUTO = 0,
    BUS_SCOPE_CAPTURE_NORMAL,
    BUS_SCOPE_CAPTURE_SINGLE,
    BUS_SCOPE_CAPTURE_HOLD
} BusScopeCaptureMode;

typedef struct
{
    uint16_t decimation;
    uint16_t trigger_mv;
    uint8_t falling;
    BusScopeCaptureMode mode;
} BusScopeCaptureConfig;

typedef struct
{
    uint16_t min_mv;
    uint16_t max_mv;
    uint16_t mean_mv;
    uint16_t rms_mv;
    uint16_t vpp_mv;
    uint16_t duty_permille;
    uint32_t freq_millihz;
    uint8_t frequency_valid;
    uint8_t undersampled;
} BusScopeCaptureStats;

typedef struct
{
    /* All three traces use raw 16-bit ADC codes, not millivolts. */
    uint16_t samples[BUS_SCOPE_CAPTURE_SAMPLES];
    uint16_t min[BUS_SCOPE_CAPTURE_SAMPLES];
    uint16_t max[BUS_SCOPE_CAPTURE_SAMPLES];
    uint32_t period_us;
    uint32_t sequence;
    uint16_t trigger_index;
    uint8_t triggered;
    BusScopeCaptureStats stats;
} BusScopeCaptureFrame;

typedef struct
{
    uint64_t sum_squares;
    uint32_t sum;
    uint32_t period_sum;
    uint32_t high_sum;
    uint32_t first_period;
    uint32_t first_high;
    uint32_t min_period;
    uint32_t max_period;
    uint16_t min;
    uint16_t max;
    uint16_t cycles;
    uint16_t bad_cycles;
    uint16_t first_end_offset;
    uint8_t first_bad;
} BusScopeCaptureBin;

/* One sampling-task-owned context; no allocation or interrupt-time processing.
 * Frequency uses raw Schmitt crossings at trigger_mv (+/-25 mV hysteresis).
 * At least two complete cycles must fit in the frame, each with >=10 samples
 * and >=2 samples in each level. Period spread >25% invalidates the result.
 * This is a sampling-quality check, not an anti-alias filter: signals above
 * Nyquist can alias and need analog input bandwidth limiting.
 */
typedef struct
{
    BusScopeCaptureConfig config;
    uint8_t stopped;
    uint8_t waiting;
    uint32_t sequence;
    uint32_t frames_dropped;
    BusScopeCaptureBin bins[BUS_SCOPE_CAPTURE_SAMPLES];
    BusScopeCaptureBin partial;
    BusScopeCaptureFrame latest;
    uint32_t tick;
    uint32_t last_rise;
    uint32_t high_width;
    uint32_t auto_samples;
    uint16_t write_index;
    uint16_t history_count;
    uint16_t partial_count;
    uint16_t post_bins;
    uint16_t threshold_low;
    uint16_t threshold_high;
    uint8_t schmitt_state;
    uint8_t cycle_started;
    uint8_t cycle_fell;
    uint8_t frame_ready;
} BusScopeCapture;

/* Defaults: 50 us/bin, rising 1650 mV, AUTO. Invalid decimation uses 5. */
void BusScopeCapture_Init(BusScopeCapture *capture);
/* Resets partial acquisition and rearms SINGLE, even with the same config.
 * HOLD keeps an already completed pending frame available for TakeFrame.
 */
void BusScopeCapture_Configure(BusScopeCapture *capture,
                               const BusScopeCaptureConfig *config);
void BusScopeCapture_Feed(BusScopeCapture *capture, const uint16_t *samples,
                         size_t count);
uint8_t BusScopeCapture_TakeFrame(BusScopeCapture *capture,
                                BusScopeCaptureFrame *frame);
/* A DMA loss invalidates history/cycle state. A stopped SINGLE stays stopped. */
void BusScopeCapture_Gap(BusScopeCapture *capture);

#ifdef __cplusplus
}
#endif

#endif
