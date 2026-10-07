#ifndef BUS_SCOPE_OUTPUT_H
#define BUS_SCOPE_OUTPUT_H

#include <stdint.h>

#define OUTPUT_PWM_MAX_HZ 100000U

typedef enum
{
    OUTPUT_FIELD_FREQ = 0, OUTPUT_FIELD_STEP, OUTPUT_FIELD_DUTY,
    OUTPUT_FIELD_ENABLE, OUTPUT_FIELD_COUNT
} BusScopeOutputField;

typedef struct
{
    uint32_t frequency_hz;
    uint32_t frequency_step_hz;
    uint16_t duty_permille;
    uint8_t enabled;
} BusScopeOutputConfig;

typedef struct
{
    uint16_t prescaler;
    uint16_t autoreload;
    uint32_t compare;
    uint32_t actual_millihz;
} BusScopeOutputTiming;

typedef enum
{
    OUTPUT_ERROR_NONE = 0, OUTPUT_ERROR_NOT_INITIALIZED,
    OUTPUT_ERROR_CONFIG, OUTPUT_ERROR_CLOCK, OUTPUT_ERROR_START
} BusScopeOutputError;

typedef struct
{
    BusScopeOutputConfig requested;
    BusScopeOutputConfig applied;
    BusScopeOutputTiming timing;
    uint32_t revision;
    uint32_t failures;
    BusScopeOutputError error;
    uint8_t ready;
    uint8_t pending;
} BusScopeOutputStatus;

void BusScope_OutputDefault(BusScopeOutputConfig *config);
void BusScope_OutputAdjust(BusScopeOutputConfig *config, BusScopeOutputField field, int direction);
uint8_t BusScope_OutputValidate(const BusScopeOutputConfig *config);
uint8_t BusScope_OutputTiming(uint32_t clock_hz, uint32_t rate_hz,
                            uint16_t duty_permille, BusScopeOutputTiming *timing);

/* Hardware backend: caller serializes Init/Apply; TIM1 generates PWM independently. */
uint8_t BusScope_OutputInit(void);
uint8_t BusScope_OutputApply(const BusScopeOutputConfig *config);
/* Coherent task-context snapshot. Timing belongs to applied, not requested. */
void BusScope_OutputSnapshot(BusScopeOutputStatus *status);
void BusScope_OutputUpdateIRQ(void);

#endif
