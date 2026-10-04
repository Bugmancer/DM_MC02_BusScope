#ifndef BUS_SCOPE_SIGNAL_H
#define BUS_SCOPE_SIGNAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    uint8_t valid;
    uint32_t freq_hz;
    uint32_t duty_permille;
    uint32_t high_mv;
    uint32_t low_mv;
} BusScopePwmMeasure;

uint32_t BusScope_AdcToMv(uint16_t raw);

/* Samples must be uniformly spaced. Frequency and duty use complete cycles only.
 * Invalid PWM keeps frequency/duty zero; nonempty input still reports min/max.
 * A null result is ignored. Null samples, zero count or zero period clear result.
 */
void BusScope_MeasurePwm(const uint16_t *samples, uint16_t count,
                         uint32_t sample_period_us, BusScopePwmMeasure *result);

#ifdef __cplusplus
}
#endif

#endif
