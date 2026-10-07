#include "bus_scope_output.h"
#include "main.h"
#include "tim.h"
#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>

static uint8_t s_initialized;
static volatile BusScopeOutputStatus s_status;
static BusScopeOutputTiming s_pending_timing;

static uint32_t timer_clock(void)
{
    uint32_t peripheral = HAL_RCC_GetPCLK2Freq();
    uint32_t hclk = HAL_RCC_GetHCLKFreq();
    if (peripheral == 0U || hclk == 0U) return 0U;
    if ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U)
        return hclk / peripheral <= 4U ? hclk : peripheral * 4U;
    return hclk == peripheral ? peripheral : peripheral * 2U;
}

static void output_pin(uint8_t alternate)
{
    GPIO_InitTypeDef gpio = {0};
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = alternate ? GPIO_MODE_AF_PP : GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOE, &gpio);
}

/* Task callers exclude TIM1_UP; the ISR is the only other writer. */
static void finish_pending(void)
{
    if (s_status.pending && __HAL_TIM_GET_FLAG(&htim1, TIM_FLAG_UPDATE))
    {
        __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
        __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
        s_status.applied = s_status.requested;
        s_status.timing = s_pending_timing;
        s_status.pending = 0U;
        s_status.revision++;
    }
}

static void output_stop(void)
{
    __HAL_TIM_DISABLE_IT(&htim1, TIM_IT_UPDATE);
    (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    CLEAR_BIT(htim1.Instance->CR1, TIM_CR1_UDIS);
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    output_pin(0U);
    s_status.pending = 0U;
    s_status.applied.enabled = 0U;
    s_status.timing = (BusScopeOutputTiming){0};
}

static uint8_t output_failure(BusScopeOutputError error)
{
    s_status.error = error;
    s_status.failures++;
    s_status.revision++;
    return 0U;
}

uint8_t BusScope_OutputInit(void)
{
    taskENTER_CRITICAL();
    if (!s_initialized)
    {
        BusScopeOutputConfig config;
        __HAL_RCC_GPIOE_CLK_ENABLE();
        output_stop();
        BusScope_OutputDefault(&config);
        config.enabled = 0U;
        s_status.requested = config;
        s_status.applied = config;
        /* TIM1 is exclusively owned here: upcounting, RCR=0, CH3 PWM1.
         * URS excludes software UG from the one-shot update interrupt. */
        SET_BIT(htim1.Instance->CR1, TIM_CR1_ARPE | TIM_CR1_URS);
        __HAL_TIM_ENABLE_OCxPRELOAD(&htim1, TIM_CHANNEL_3);
        HAL_NVIC_SetPriority(TIM1_UP_IRQn, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY, 0U);
        HAL_NVIC_ClearPendingIRQ(TIM1_UP_IRQn);
        HAL_NVIC_EnableIRQ(TIM1_UP_IRQn);
        s_status.ready = 1U;
        s_status.revision++;
        s_initialized = 1U;
    }
    taskEXIT_CRITICAL();
    return 1U;
}

uint8_t BusScope_OutputApply(const BusScopeOutputConfig *config)
{
    BusScopeOutputTiming timing = {0};
    BusScopeOutputError error = OUTPUT_ERROR_NONE;
    uint8_t success = 1U;
    /* The divider search never runs with interrupts masked. */
    if (!BusScope_OutputValidate(config)) error = OUTPUT_ERROR_CONFIG;
    else if (config->enabled && !BusScope_OutputTiming(timer_clock(), config->frequency_hz,
                                                       config->duty_permille, &timing))
        error = OUTPUT_ERROR_CLOCK;

    taskENTER_CRITICAL();
    if (!s_initialized) error = OUTPUT_ERROR_NOT_INITIALIZED;
    if (error != OUTPUT_ERROR_NONE)
    {
        success = output_failure(error);
        taskEXIT_CRITICAL();
        return success;
    }

    /* UDIS blocks shadow transfers, not counting. Retire an earlier transfer
     * before replacing pending state, even if its IRQ was delayed. */
    SET_BIT(htim1.Instance->CR1, TIM_CR1_UDIS);
    finish_pending();
    if (s_status.ready && config->frequency_hz == s_status.requested.frequency_hz &&
        config->duty_permille == s_status.requested.duty_permille &&
        config->enabled == s_status.requested.enabled)
    {
        if (config->frequency_step_hz != s_status.requested.frequency_step_hz ||
            s_status.error != OUTPUT_ERROR_NONE) s_status.revision++;
        s_status.requested.frequency_step_hz = config->frequency_step_hz;
        s_status.applied.frequency_step_hz = config->frequency_step_hz;
        s_status.error = OUTPUT_ERROR_NONE;
        CLEAR_BIT(htim1.Instance->CR1, TIM_CR1_UDIS);
        taskEXIT_CRITICAL();
        return 1U;
    }

    s_status.requested = *config;
    s_status.error = OUTPUT_ERROR_NONE;
    s_status.revision++;
    if (!config->enabled)
    {
        output_stop();
        s_status.applied = *config;
        s_status.ready = 1U;
    }
    else if (s_status.ready && s_status.applied.enabled)
    {
        __HAL_TIM_SET_PRESCALER(&htim1, timing.prescaler);
        __HAL_TIM_SET_AUTORELOAD(&htim1, timing.autoreload);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, timing.compare);
        s_pending_timing = timing;
        s_status.pending = 1U;
        __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
        __HAL_TIM_ENABLE_IT(&htim1, TIM_IT_UPDATE);
        CLEAR_BIT(htim1.Instance->CR1, TIM_CR1_UDIS);
    }
    else
    {
        output_stop();
        __HAL_TIM_SET_PRESCALER(&htim1, timing.prescaler);
        __HAL_TIM_SET_AUTORELOAD(&htim1, timing.autoreload);
        __HAL_TIM_SET_COUNTER(&htim1, 0U);
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, timing.compare);
        if (HAL_TIM_GenerateEvent(&htim1, TIM_EVENTSOURCE_UPDATE) == HAL_OK)
        {
            __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
            output_pin(1U);
            if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3) == HAL_OK)
            {
                s_status.applied = *config;
                s_status.timing = timing;
                s_status.ready = 1U;
            }
            else success = 0U;
        }
        else success = 0U;
        if (!success)
        {
            output_stop();
            s_status.ready = 0U;
            (void)output_failure(OUTPUT_ERROR_START);
        }
    }
    taskEXIT_CRITICAL();
    return success;
}

void BusScope_OutputSnapshot(BusScopeOutputStatus *status)
{
    if (status == NULL) return;
    taskENTER_CRITICAL();
    if (s_initialized) finish_pending();
    *status = s_status;
    taskEXIT_CRITICAL();
}

void BusScope_OutputUpdateIRQ(void)
{
    if (__HAL_TIM_GET_IT_SOURCE(&htim1, TIM_IT_UPDATE)) finish_pending();
}
