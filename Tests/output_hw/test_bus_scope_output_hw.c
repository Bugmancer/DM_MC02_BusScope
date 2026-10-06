#include <assert.h>
#include <stdio.h>
#include "../../../App/bus_scope_output_hw.c"

TestTimer test_tim1;
TIM_HandleTypeDef htim1 = {.Instance = &test_tim1};
static TestRcc rcc;
TestRcc *RCC = &rcc;
static uint32_t hclk = 240000000U, pclk = 120000000U;
static uint32_t pin_mode, pin_value;
static uint8_t fail_pwm, fail_event;

uint32_t HAL_RCC_GetPCLK2Freq(void) { return pclk; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return hclk; }
void HAL_GPIO_WritePin(void *port, uint32_t pin, uint32_t value)
{
    assert(port == GPIOE && pin == GPIO_PIN_13);
    pin_value = value;
}
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{
    assert(port == GPIOE && gpio->Pin == GPIO_PIN_13);
    pin_mode = gpio->Mode;
}
uint32_t HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(channel == TIM_CHANNEL_3);
    timer->Instance->enabled = timer->Instance->moe = 0U;
    return HAL_OK;
}
uint32_t HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(channel == TIM_CHANNEL_3 && pin_mode == GPIO_MODE_AF_PP);
    if (fail_pwm) return HAL_ERROR;
    timer->Instance->enabled = timer->Instance->moe = 1U;
    return HAL_OK;
}
uint32_t HAL_TIM_GenerateEvent(TIM_HandleTypeDef *timer, uint32_t event)
{
    assert(event == TIM_EVENTSOURCE_UPDATE && !timer->Instance->enabled);
    return fail_event ? HAL_ERROR : HAL_OK;
}

static void assert_off(void)
{
    assert(pin_mode == GPIO_MODE_OUTPUT_PP && pin_value == GPIO_PIN_RESET);
    assert(!test_tim1.moe && !test_tim1.enabled);
    assert(BusScope_OutputActualMillihz() == 0U);
}

int main(void)
{
    BusScopeOutputConfig c;
    BusScope_OutputDefault(&c);
    assert(BusScope_OutputInit());
    assert_off();
    assert(BusScope_OutputApply(&c));
    assert(test_tim1.moe && pin_mode == GPIO_MODE_AF_PP);
    assert(BusScope_OutputActualMillihz() == 10000U);
    assert(test_tim1.CCR3 * 2U >= test_tim1.ARR + 1U && test_tim1.CCR3 * 2U <= test_tim1.ARR + 2U);
    c.enabled = 0U;
    assert(BusScope_OutputApply(&c) && BusScope_OutputReady());
    assert_off();
    c.enabled = 1U; c.frequency_hz = 100000U; c.duty_permille = 250U;
    assert(BusScope_OutputApply(&c));
    assert(BusScope_OutputActualMillihz() == 100000000U);
    assert(test_tim1.CCR3 * 4U == test_tim1.ARR + 1U);
    fail_pwm = 1U;
    assert(!BusScope_OutputApply(&c) && !BusScope_OutputReady());
    assert_off();
    fail_pwm = 0U; fail_event = 1U;
    assert(!BusScope_OutputApply(&c) && !BusScope_OutputReady());
    assert_off();
    fail_event = 0U;
    c.duty_permille = 1000U;
    assert(BusScope_OutputApply(&c) && test_tim1.CCR3 == test_tim1.ARR + 1U);
    c.duty_permille = 0U;
    assert(BusScope_OutputApply(&c) && test_tim1.CCR3 == 0U);
    c.frequency_hz = 0U;
    assert(!BusScope_OutputApply(&c)); assert_off();
    /* Both TIMPRE clock modes, including a divided APB clock. */
    RCC->CFGR = RCC_CFGR_TIMPRE; pclk = 60000000U;
    c.frequency_hz = 10U;
    assert(BusScope_OutputApply(&c) && BusScope_OutputActualMillihz() == 10000U);
    pclk = 30000000U;
    assert(BusScope_OutputApply(&c) && BusScope_OutputActualMillihz() == 10000U);
    puts("bus_scope_output_hw: all tests passed");
    return 0;
}
