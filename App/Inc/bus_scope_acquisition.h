#ifndef BUS_SCOPE_ACQUISITION_H
#define BUS_SCOPE_ACQUISITION_H

#include <stdint.h>

#define BUS_SCOPE_ADC_BLOCK_SAMPLES 512U
#define BUS_SCOPE_ADC_RATE_HZ       100000U
#define BUS_SCOPE_ADC_PERIOD_US     10U

typedef struct
{
    uint32_t blocks;
    uint32_t lost_blocks;
    uint32_t errors;
    uint32_t restarts;
    uint32_t last_error;
    uint8_t running;
} BusScopeAcquisitionStatus;

void BusScope_AcquisitionInit(void);
/* Start, ReadBlock and Stop belong to the sampling task. DMA wakes this task. */
uint8_t BusScope_AcquisitionStart(void);
void BusScope_AcquisitionStop(void);
/* A successful block contains PA0 samples only; gap invalidates prior history. */
uint8_t BusScope_AcquisitionReadBlock(uint16_t samples[BUS_SCOPE_ADC_BLOCK_SAMPLES], uint8_t *gap);
uint16_t BusScope_AcquisitionKeyRaw(void);
void BusScope_AcquisitionSnapshot(BusScopeAcquisitionStatus *status);

#endif
