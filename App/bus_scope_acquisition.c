#include "bus_scope_acquisition.h"
#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>
#include <string.h>

#define ADC_CHANNELS             2U
#define ADC_HALF_WORDS           (BUS_SCOPE_ADC_BLOCK_SAMPLES * ADC_CHANNELS)
#define ADC_BUFFER_WORDS         (ADC_HALF_WORDS * 2U)
#define ADC_COPY_LIMIT_MS        5U
#define ADC_RETRY_MS             100U
#define ADC_ERROR_TIMER          0x80000000UL
#define ADC_ERROR_START          0x40000000UL
#define ADC_ERROR_STALLED        0x20000000UL
#define ADC_ERROR_STOP           0x10000000UL

/* DMA1 cannot access DTCM. Each half owns whole, private M7 cache lines. */
#if defined(__CC_ARM)
static volatile uint16_t s_dma[ADC_BUFFER_WORDS] __attribute__((section("ADC_DMA"), zero_init, aligned(32)));
#else
static volatile uint16_t s_dma[ADC_BUFFER_WORDS] __attribute__((section("ADC_DMA"), aligned(32)));
#endif
static volatile BusScopeAcquisitionStatus s_status;
static volatile uint32_t s_completed;
static volatile uint32_t s_last_irq_ms;
static volatile uint8_t s_completed_half;
static volatile uint8_t s_recover;
static volatile uint16_t s_key_raw = 65535U;
static uint32_t s_consumed;
static volatile uint32_t s_retry_ms;
static uint8_t s_gap = 1U;
static uint8_t s_initialized;
static uint8_t s_calibrated;
static TaskHandle_t s_sample_task;

static uint32_t timer_clock(void)
{
    uint32_t peripheral = HAL_RCC_GetPCLK1Freq();
    uint32_t hclk = HAL_RCC_GetHCLKFreq();
    if (peripheral == 0U || hclk == 0U) return 0U;
    if ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U)
        return hclk / peripheral <= 4U ? hclk : peripheral * 4U;
    return hclk == peripheral ? peripheral : peripheral * 2U;
}

static uint8_t timer_prepare(void)
{
    uint32_t clock = timer_clock();
    uint32_t period = clock / BUS_SCOPE_ADC_RATE_HZ;
    if (clock % BUS_SCOPE_ADC_RATE_HZ != 0U || period == 0U || period > 65536U) return 0U;
    TIM6->CR1 = TIM_CR1_ARPE;
    TIM6->CR2 = 0U;
    TIM6->DIER = 0U;
    TIM6->PSC = 0U;
    TIM6->ARR = period - 1U;
    TIM6->CNT = 0U;
    /* Load PSC/ARR before arming ADC; UG must not create an extra sample. */
    TIM6->EGR = TIM_EGR_UG;
    TIM6->SR = 0U;
    TIM6->CR2 = TIM_TRGO_UPDATE;
    return 1U;
}

static void task_error(uint32_t error)
{
    TIM6->CR1 &= ~TIM_CR1_CEN;
    taskENTER_CRITICAL();
    s_status.running = 0U;
    s_status.errors++;
    s_status.last_error = error;
    s_recover = 1U;
    taskEXIT_CRITICAL();
    s_gap = 1U;
    s_retry_ms = HAL_GetTick();
}

static uint8_t start_conversion(void)
{
    if (!timer_prepare())
    {
        task_error(ADC_ERROR_TIMER);
        return 0U;
    }
    taskENTER_CRITICAL();
    s_completed = 0U;
    s_consumed = 0U;
    s_completed_half = 0U;
    s_recover = 0U;
    taskEXIT_CRITICAL();
    s_gap = 1U;
    s_key_raw = 65535U;
    if (!s_calibrated)
    {
        if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_CALIB_OFFSET_LINEARITY, ADC_SINGLE_ENDED) != HAL_OK)
        {
            task_error(ADC_ERROR_START | hadc1.ErrorCode);
            return 0U;
        }
        s_calibrated = 1U;
    }
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)s_dma, ADC_BUFFER_WORDS) != HAL_OK)
    {
        task_error(ADC_ERROR_START | hadc1.ErrorCode);
        return 0U;
    }
    taskENTER_CRITICAL();
    s_last_irq_ms = HAL_GetTick();
    s_status.running = 1U;
    taskEXIT_CRITICAL();
    __DMB();
    TIM6->CR1 |= TIM_CR1_CEN;
    return 1U;
}

void BusScope_AcquisitionInit(void)
{
    if (s_initialized) return;
    __HAL_RCC_D2SRAM1_CLK_ENABLE();
    __HAL_RCC_TIM6_CLK_ENABLE();
    __HAL_RCC_TIM6_FORCE_RESET();
    __HAL_RCC_TIM6_RELEASE_RESET();
    memset((void *)s_dma, 0, sizeof(s_dma));
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
        SCB_CleanInvalidateDCache_by_Addr((uint32_t *)(void *)s_dma, (int32_t)sizeof(s_dma));
    s_initialized = 1U;
}

uint8_t BusScope_AcquisitionStart(void)
{
    BusScope_AcquisitionInit();
    s_sample_task = xTaskGetCurrentTaskHandle();
    if (s_status.running) return 1U;
    if (s_recover && HAL_ADC_Stop_DMA(&hadc1) != HAL_OK)
    {
        task_error(ADC_ERROR_STOP | hadc1.ErrorCode);
        return 0U;
    }
    return start_conversion();
}

void BusScope_AcquisitionStop(void)
{
    if (!s_initialized) return;
    TIM6->CR1 &= ~TIM_CR1_CEN;
    taskENTER_CRITICAL();
    s_status.running = 0U;
    s_recover = 0U;
    taskEXIT_CRITICAL();
    (void)HAL_ADC_Stop_DMA(&hadc1);
    s_sample_task = NULL;
    s_key_raw = 65535U;
    s_gap = 1U;
}

static uint8_t half_is_idle(uint8_t half)
{
    uint32_t remaining = __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle);
    return half == 0U ? (remaining != 0U && remaining <= ADC_HALF_WORDS) :
        (remaining > ADC_HALF_WORDS && remaining <= ADC_BUFFER_WORDS);
}

static uint8_t half_was_reused(uint8_t half)
{
    uint32_t boundary = half == 0U ? __HAL_DMA_GET_TC_FLAG_INDEX(hadc1.DMA_Handle) :
        __HAL_DMA_GET_HT_FLAG_INDEX(hadc1.DMA_Handle);
    /* Sticky boundary flags also detect a full wrap while the DMA ISR waits. */
    return __HAL_DMA_GET_FLAG(hadc1.DMA_Handle, boundary) != 0U;
}

uint8_t BusScope_AcquisitionReadBlock(uint16_t samples[BUS_SCOPE_ADC_BLOCK_SAMPLES], uint8_t *gap)
{
    uint32_t sequence;
    uint32_t elapsed_blocks;
    uint32_t start_ms;
    uint32_t index;
    uint16_t key;
    uint8_t half;
    volatile uint16_t *source;
    if (samples == NULL || gap == NULL || !s_initialized) return 0U;
    *gap = 0U;
    if (s_status.running && (uint32_t)(HAL_GetTick() - s_last_irq_ms) >= ADC_RETRY_MS)
        task_error(ADC_ERROR_STALLED);
    if (s_recover)
    {
        if ((uint32_t)(HAL_GetTick() - s_retry_ms) < ADC_RETRY_MS) return 0U;
        if (HAL_ADC_Stop_DMA(&hadc1) != HAL_OK)
        {
            task_error(ADC_ERROR_STOP | hadc1.ErrorCode);
            return 0U;
        }
        if (start_conversion()) s_status.restarts++;
        return 0U;
    }
    if (!s_status.running) return 0U;
    taskENTER_CRITICAL();
    sequence = s_completed;
    half = s_completed_half;
    taskEXIT_CRITICAL();
    elapsed_blocks = sequence - s_consumed;
    if (elapsed_blocks == 0U) return 0U;
    s_consumed = sequence;
    if (elapsed_blocks > 1U)
    {
        s_status.lost_blocks += elapsed_blocks - 1U;
        s_gap = 1U;
    }
    start_ms = HAL_GetTick();
    if (!half_is_idle(half) || half_was_reused(half))
    {
        s_status.lost_blocks++;
        s_gap = 1U;
        return 0U;
    }
    source = &s_dma[(uint32_t)half * ADC_HALF_WORDS];
    if ((SCB->CCR & SCB_CCR_DC_Msk) != 0U)
        SCB_InvalidateDCache_by_Addr((uint32_t *)(void *)source, (int32_t)(ADC_HALF_WORDS * sizeof(uint16_t)));
    for (index = 0U; index < BUS_SCOPE_ADC_BLOCK_SAMPLES; index++)
        samples[index] = source[index * ADC_CHANNELS];
    key = source[ADC_HALF_WORDS - 1U];
    __DMB();
    /* DMA keeps running. Reject a half reused during this copy, including a
       full rotation while a higher-priority ISR prevented DMA notifications. */
    if (!half_is_idle(half) || half_was_reused(half) || sequence != s_completed || s_recover ||
        (uint32_t)(HAL_GetTick() - start_ms) >= ADC_COPY_LIMIT_MS)
    {
        s_status.lost_blocks++;
        s_gap = 1U;
        return 0U;
    }
    s_key_raw = key;
    s_status.blocks++;
    *gap = s_gap;
    s_gap = 0U;
    return 1U;
}

uint16_t BusScope_AcquisitionKeyRaw(void)
{
    return s_status.running ? s_key_raw : 65535U;
}

void BusScope_AcquisitionSnapshot(BusScopeAcquisitionStatus *status)
{
    if (status == NULL) return;
    taskENTER_CRITICAL();
    *status = s_status;
    taskEXIT_CRITICAL();
}

static void completed_from_isr(uint8_t half)
{
    BaseType_t wake = pdFALSE;
    if (!s_status.running) return;
    s_completed_half = half;
    s_completed++;
    s_last_irq_ms = HAL_GetTick();
    if (s_sample_task != NULL) vTaskNotifyGiveFromISR(s_sample_task, &wake);
    portYIELD_FROM_ISR(wake);
}

void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1) completed_from_isr(0U);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc == &hadc1) completed_from_isr(1U);
}

void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc)
{
    BaseType_t wake = pdFALSE;
    if (hadc != &hadc1) return;
    TIM6->CR1 &= ~TIM_CR1_CEN;
    s_status.running = 0U;
    s_status.errors++;
    s_status.last_error = hadc->ErrorCode;
    s_retry_ms = HAL_GetTick();
    s_recover = 1U;
    if (s_sample_task != NULL) vTaskNotifyGiveFromISR(s_sample_task, &wake);
    portYIELD_FROM_ISR(wake);
}
