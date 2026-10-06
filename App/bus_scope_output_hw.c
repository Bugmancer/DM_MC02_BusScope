#include "bus_scope_output.h"
#include "main.h"
#include "tim.h"

static uint8_t s_initialized;
static volatile uint8_t s_ready;
static volatile uint32_t s_actual_millihz;

static uint32_t timer_clock(void)
{
    uint32_t peripheral = HAL_RCC_GetPCLK2Freq();
    uint32_t hclk = HAL_RCC_GetHCLKFreq();
    if ((RCC->CFGR & RCC_CFGR_TIMPRE) != 0U)
        return hclk / peripheral <= 4U ? hclk : peripheral * 4U;
    return hclk == peripheral ? peripheral : peripheral * 2U;
}

static void output_pin(uint8_t alternate)
{
    GPIO_InitTypeDef gpio = {0};
    /* Keep the disabled/error output actively low rather than floating. */
    HAL_GPIO_WritePin(GPIOE, GPIO_PIN_13, GPIO_PIN_RESET);
    gpio.Pin = GPIO_PIN_13;
    gpio.Mode = alternate ? GPIO_MODE_AF_PP : GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF1_TIM1;
    HAL_GPIO_Init(GPIOE, &gpio);
}

static void output_stop(void)
{
    s_ready = 0U;
    s_actual_millihz = 0U;
    (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_3);
    output_pin(0U);
}

uint8_t BusScope_OutputInit(void)
{
    if (s_initialized) return 1U;
    __HAL_RCC_GPIOE_CLK_ENABLE();
    output_stop();
    s_initialized = 1U;
    return 1U;
}

uint8_t BusScope_OutputApply(const BusScopeOutputConfig *config)
{
    BusScopeOutputTiming timing;
    output_stop();
    if (!s_initialized || !BusScope_OutputValidate(config)) return 0U;
    if (!config->enabled)
    {
        s_ready = 1U;
        return 1U;
    }
    if (!BusScope_OutputTiming(timer_clock(), config->frequency_hz,
                              config->duty_permille, &timing)) return 0U;
    __HAL_TIM_SET_PRESCALER(&htim1, timing.prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim1, timing.autoreload);
    __HAL_TIM_SET_COUNTER(&htim1, 0U);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_3, timing.compare);
    if (HAL_TIM_GenerateEvent(&htim1, TIM_EVENTSOURCE_UPDATE) != HAL_OK) return 0U;
    output_pin(1U);
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_3) != HAL_OK)
    {
        output_stop();
        return 0U;
    }
    s_actual_millihz = timing.actual_millihz;
    s_ready = 1U;
    return 1U;
}

uint8_t BusScope_OutputReady(void) { return s_ready; }
uint32_t BusScope_OutputActualMillihz(void) { return s_actual_millihz; }
