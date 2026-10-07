#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../App/bus_scope_acquisition.c"

TIM_TypeDef test_tim6;
RCC_TypeDef test_rcc;
SCB_Type test_scb;
static DMA_HandleTypeDef test_dma;
ADC_HandleTypeDef hadc1 = {0U, &test_dma};
static uint32_t tick;
static uint32_t pclk;
static uint32_t hclk;
static uint32_t calibration_result;
static uint32_t start_result;
static uint32_t stop_result;
static unsigned calibrations;
static unsigned starts;
static unsigned stops;
static unsigned notifications;
static unsigned clean_calls;
static unsigned invalidate_calls;
static uint32_t *invalidated_address;
static int32_t invalidated_size;
static unsigned copy_fault;

uint32_t HAL_GetTick(void) { return tick; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return pclk; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return hclk; }
uint32_t HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *adc, uint32_t mode, uint32_t input)
{
    assert(adc == &hadc1 && mode == ADC_CALIB_OFFSET_LINEARITY && input == ADC_SINGLE_ENDED);
    calibrations++;
    return calibration_result;
}
uint32_t HAL_ADC_Start_DMA(ADC_HandleTypeDef *adc, uint32_t *data, uint32_t count)
{
    assert(adc == &hadc1 && data == (uint32_t *)(void *)s_dma && count == 2048U);
    assert((TIM6->CR1 & TIM_CR1_CEN) == 0U);
    test_dma.remaining = count;
    starts++;
    return start_result;
}
uint32_t HAL_ADC_Stop_DMA(ADC_HandleTypeDef *adc)
{
    assert(adc == &hadc1 && (TIM6->CR1 & TIM_CR1_CEN) == 0U);
    stops++;
    return stop_result;
}
void SCB_CleanInvalidateDCache_by_Addr(uint32_t *address, int32_t size)
{
    assert(address == (uint32_t *)(void *)s_dma && size == 4096);
    assert((TIM6->CR1 & TIM_CR1_CEN) == 0U);
    clean_calls++;
}
void SCB_InvalidateDCache_by_Addr(uint32_t *address, int32_t size)
{
    invalidate_calls++;
    invalidated_address = address;
    invalidated_size = size;
    if (copy_fault == 1U)
    {
        test_dma.remaining = 2048U;
        HAL_ADC_ConvCpltCallback(&hadc1);
    }
    else if (copy_fault == 2U) tick += 11U;
    else if (copy_fault == 3U) test_dma.remaining = 2048U;
    else if (copy_fault == 4U)
    {
        hadc1.ErrorCode = 2U;
        HAL_ADC_ErrorCallback(&hadc1);
    }
    else if (copy_fault == 5U) test_dma.flags = 1U;
}
TaskHandle_t xTaskGetCurrentTaskHandle(void) { return &test_dma; }
void vTaskNotifyGiveFromISR(TaskHandle_t task, BaseType_t *wake)
{
    assert(task == &test_dma);
    *wake = 1;
    notifications++;
}

static void reset(void)
{
    memset(&test_tim6, 0, sizeof(test_tim6));
    memset((void *)&s_status, 0, sizeof(s_status));
    test_rcc.CFGR = 0U;
    test_scb.CCR = SCB_CCR_DC_Msk;
    test_dma.remaining = 0U;
    test_dma.flags = 0U;
    hadc1.ErrorCode = 0U;
    tick = 1000U;
    pclk = 120000000U;
    hclk = 240000000U;
    calibration_result = HAL_OK;
    start_result = HAL_OK;
    stop_result = HAL_OK;
    calibrations = starts = stops = notifications = clean_calls = invalidate_calls = 0U;
    copy_fault = 0U;
    s_completed = s_consumed = s_last_irq_ms = s_retry_ms = 0U;
    s_completed_half = s_recover = s_initialized = s_calibrated = 0U;
    s_gap = 1U;
    s_key_raw = 65535U;
    s_sample_task = NULL;
}

static void complete_half(uint8_t half, uint16_t first, uint16_t key)
{
    uint32_t index;
    uint32_t offset = half * ADC_HALF_WORDS;
    for (index = 0U; index < BUS_SCOPE_ADC_BLOCK_SAMPLES; index++)
    {
        s_dma[offset + index * 2U] = (uint16_t)(first + index);
        s_dma[offset + index * 2U + 1U] = key;
    }
    test_dma.remaining = half == 0U ? 1024U : 2048U;
    if (half == 0U) HAL_ADC_ConvHalfCpltCallback(&hadc1);
    else HAL_ADC_ConvCpltCallback(&hadc1);
}

static void test_start_and_handoff(void)
{
    uint16_t samples[512];
    uint8_t gap = 0U;
    BusScopeAcquisitionStatus status;
    reset();
    assert(BusScope_AcquisitionStart());
    assert(calibrations == 1U && starts == 1U && clean_calls == 1U);
    assert(TIM6->ARR == 2399U && TIM6->PSC == 0U && TIM6->CR2 == TIM_TRGO_UPDATE);
    assert((TIM6->CR1 & TIM_CR1_CEN) != 0U);
    assert(!BusScope_AcquisitionReadBlock(samples, &gap));
    complete_half(0U, 100U, 26100U);
    assert(BusScope_AcquisitionReadBlock(samples, &gap) && gap == 1U);
    assert(samples[0] == 100U && samples[511] == 611U);
    assert(BusScope_AcquisitionKeyRaw() == 26100U);
    assert(invalidated_address == (uint32_t *)(void *)s_dma && invalidated_size == 2048);
    assert(!BusScope_AcquisitionReadBlock(samples, &gap));
    complete_half(1U, 800U, 65535U);
    assert(BusScope_AcquisitionReadBlock(samples, &gap) && gap == 0U);
    assert(samples[0] == 800U && samples[511] == 1311U);
    assert(invalidated_address == (uint32_t *)(void *)&s_dma[1024] && invalidated_size == 2048);
    BusScope_AcquisitionSnapshot(&status);
    assert(status.blocks == 2U && status.lost_blocks == 0U && notifications == 2U);
    BusScope_AcquisitionStop();
    assert(!s_status.running && BusScope_AcquisitionKeyRaw() == 65535U && stops == 1U);
    assert(BusScope_AcquisitionStart() && calibrations == 1U && clean_calls == 1U);
}

static void test_skip_and_reuse(void)
{
    uint16_t samples[512];
    uint8_t gap;
    unsigned fault;
    reset();
    assert(BusScope_AcquisitionStart());
    complete_half(0U, 1U, 65535U);
    complete_half(1U, 2U, 65535U);
    assert(BusScope_AcquisitionReadBlock(samples, &gap) && gap == 1U);
    assert(samples[0] == 2U && s_status.lost_blocks == 1U);
    complete_half(0U, 3U, 65535U);
    test_dma.remaining = 2048U;
    assert(!BusScope_AcquisitionReadBlock(samples, &gap));
    assert(s_status.lost_blocks == 2U && invalidate_calls == 1U);
    for (fault = 1U; fault <= 5U; fault++)
    {
        reset();
        assert(BusScope_AcquisitionStart());
        complete_half(0U, 9U, 13000U);
        copy_fault = fault;
        assert(!BusScope_AcquisitionReadBlock(samples, &gap));
        assert(s_status.blocks == 0U && s_status.lost_blocks == 1U);
        assert(BusScope_AcquisitionKeyRaw() == 65535U);
        copy_fault = 0U;
    }
}

static void test_recovery(void)
{
    uint16_t samples[512];
    uint8_t gap;
    reset();
    assert(BusScope_AcquisitionStart());
    complete_half(0U, 5U, 50U);
    assert(BusScope_AcquisitionReadBlock(samples, &gap));
    hadc1.ErrorCode = 2U;
    HAL_ADC_ErrorCallback(&hadc1);
    assert(!s_status.running && !(TIM6->CR1 & TIM_CR1_CEN));
    assert(s_status.errors == 1U && s_status.last_error == 2U);
    assert(!BusScope_AcquisitionReadBlock(samples, &gap) && starts == 1U);
    tick += 100U;
    assert(!BusScope_AcquisitionReadBlock(samples, &gap));
    assert(stops == 1U && starts == 2U && s_status.restarts == 1U && s_status.running);
    complete_half(0U, 900U, 65535U);
    assert(BusScope_AcquisitionReadBlock(samples, &gap) && gap == 1U);
    tick += 100U;
    assert(!BusScope_AcquisitionReadBlock(samples, &gap));
    assert(s_status.last_error == ADC_ERROR_STALLED && !s_status.running);
    tick += 100U;
    stop_result = HAL_ERROR;
    assert(!BusScope_AcquisitionReadBlock(samples, &gap));
    assert((s_status.last_error & ADC_ERROR_STOP) != 0U && starts == 2U);
    stop_result = HAL_OK;
    tick += 100U;
    assert(!BusScope_AcquisitionReadBlock(samples, &gap));
    assert(starts == 3U && s_status.restarts == 2U);
}

static void test_timer_and_start_failure(void)
{
    reset();
    pclk = 30000000U;
    test_rcc.CFGR = RCC_CFGR_TIMPRE;
    assert(BusScope_AcquisitionStart() && TIM6->ARR == 1199U);
    reset();
    pclk = 60000000U;
    test_rcc.CFGR = RCC_CFGR_TIMPRE;
    assert(BusScope_AcquisitionStart() && TIM6->ARR == 2399U);
    reset();
    pclk = hclk;
    assert(BusScope_AcquisitionStart() && TIM6->ARR == 2399U);
    reset();
    pclk = 120000001U;
    assert(!BusScope_AcquisitionStart() && s_status.last_error == ADC_ERROR_TIMER);
    assert(starts == 0U && !s_status.running);
    reset();
    calibration_result = HAL_ERROR;
    assert(!BusScope_AcquisitionStart() && !s_calibrated && starts == 0U);
    reset();
    start_result = HAL_ERROR;
    assert(!BusScope_AcquisitionStart() && !s_status.running);
    assert((TIM6->CR1 & TIM_CR1_CEN) == 0U);
}

int main(void)
{
    test_start_and_handoff();
    test_skip_and_reuse();
    test_recovery();
    test_timer_and_start_failure();
    puts("acquisition tests passed: timer, DMA ownership, cache, keypad, gaps and recovery");
    return 0;
}
