#include <assert.h>
#include <stdio.h>
#include "../../App/bus_scope_output_hw.c"

TestTimer test_tim1;
TIM_HandleTypeDef htim1 = {.Instance = &test_tim1};
static TestRcc rcc;
TestRcc *RCC = &rcc;
static uint32_t hclk = 240000000U, pclk = 120000000U;
static uint32_t pin_mode, pin_value, critical_depth, irq_enabled, irq_pending;
static uint32_t start_count, stop_count, event_count, counter_writes, irq_count;
static uint32_t inject_overflow, guarded_writes, guard_psc, guard_arr, guard_ccr;
static uint8_t fail_pwm, fail_event;

void test_enter_critical(void) { critical_depth++; }
void test_exit_critical(void)
{
    assert(critical_depth > 0U);
    critical_depth--;
}

static void transfer_preloads(void)
{
    test_tim1.active_psc = test_tim1.PSC;
    test_tim1.active_arr = test_tim1.ARR;
    test_tim1.active_ccr = test_tim1.CCR3;
}

static void natural_overflow(void)
{
    if (!test_tim1.enabled) return;
    test_tim1.CNT = 0U;
    if (!(test_tim1.CR1 & TIM_CR1_UDIS))
    {
        transfer_preloads();
        test_tim1.SR |= TIM_FLAG_UPDATE;
        if (test_tim1.DIER & TIM_IT_UPDATE) irq_pending = 1U;
    }
}

static void dispatch_irq(void)
{
    assert(critical_depth == 0U);
    if (irq_enabled && irq_pending)
    {
        irq_pending = 0U;
        irq_count++;
        BusScope_OutputUpdateIRQ();
    }
}

static void guarded_write(void)
{
    assert(critical_depth > 0U);
    if (inject_overflow && test_tim1.enabled)
    {
        guarded_writes++;
        natural_overflow();
        assert(test_tim1.active_psc == guard_psc);
        assert(test_tim1.active_arr == guard_arr);
        assert(test_tim1.active_ccr == guard_ccr);
        assert(test_tim1.CR1 & TIM_CR1_UDIS);
    }
}

void test_write_prescaler(TIM_HandleTypeDef *timer, uint32_t value)
{
    timer->Instance->PSC = value;
    guarded_write();
}
void test_write_autoreload(TIM_HandleTypeDef *timer, uint32_t value)
{
    timer->Instance->ARR = value;
    if (!(timer->Instance->CR1 & TIM_CR1_ARPE)) timer->Instance->active_arr = value;
    guarded_write();
}
void test_write_counter(TIM_HandleTypeDef *timer, uint32_t value)
{
    timer->Instance->CNT = value;
    counter_writes++;
    assert(!timer->Instance->enabled);
}
void test_write_compare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value)
{
    assert(channel == TIM_CHANNEL_3);
    timer->Instance->CCR3 = value;
    if (!(timer->Instance->CCMR2 & TIM_CCMR2_OC3PE)) timer->Instance->active_ccr = value;
    guarded_write();
}
void test_enable_preload(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(channel == TIM_CHANNEL_3);
    timer->Instance->CCMR2 |= TIM_CCMR2_OC3PE;
}
void test_enable_interrupt(TIM_HandleTypeDef *timer, uint32_t interrupt)
{
    timer->Instance->DIER |= interrupt;
    if (timer->Instance->SR & interrupt) irq_pending = 1U;
}

uint32_t HAL_RCC_GetPCLK2Freq(void) { return pclk; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return hclk; }
void HAL_GPIO_WritePin(void *port, uint32_t pin, uint32_t value)
{
    assert(port == GPIOE && pin == GPIO_PIN_13 && critical_depth > 0U);
    pin_value = value;
}
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *gpio)
{
    assert(port == GPIOE && gpio->Pin == GPIO_PIN_13);
    assert(gpio->Alternate == GPIO_AF1_TIM1);
    pin_mode = gpio->Mode;
}
uint32_t HAL_TIM_PWM_Stop(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(channel == TIM_CHANNEL_3 && critical_depth > 0U);
    stop_count++;
    timer->Instance->enabled = timer->Instance->moe = 0U;
    return HAL_OK;
}
uint32_t HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(channel == TIM_CHANNEL_3 && pin_mode == GPIO_MODE_AF_PP);
    assert(critical_depth > 0U);
    start_count++;
    if (fail_pwm) return HAL_ERROR;
    timer->Instance->enabled = timer->Instance->moe = 1U;
    return HAL_OK;
}
uint32_t HAL_TIM_GenerateEvent(TIM_HandleTypeDef *timer, uint32_t event)
{
    assert(event == TIM_EVENTSOURCE_UPDATE && !timer->Instance->enabled);
    assert(critical_depth > 0U);
    event_count++;
    if (fail_event) return HAL_ERROR;
    assert(!(timer->Instance->CR1 & TIM_CR1_UDIS));
    transfer_preloads();
    timer->Instance->CNT = 0U;
    if (!(timer->Instance->CR1 & TIM_CR1_URS)) timer->Instance->SR |= TIM_FLAG_UPDATE;
    return HAL_OK;
}
void HAL_NVIC_SetPriority(uint32_t irq, uint32_t priority, uint32_t subpriority)
{
    assert(irq == TIM1_UP_IRQn && priority >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY);
    assert(subpriority == 0U);
}
void HAL_NVIC_ClearPendingIRQ(uint32_t irq)
{
    assert(irq == TIM1_UP_IRQn);
    irq_pending = 0U;
}
void HAL_NVIC_EnableIRQ(uint32_t irq)
{
    assert(irq == TIM1_UP_IRQn);
    irq_enabled = 1U;
}

static BusScopeOutputStatus snapshot(void)
{
    BusScopeOutputStatus status;
    uint32_t depth = critical_depth;
    BusScope_OutputSnapshot(&status);
    assert(critical_depth == depth);
    return status;
}

static void assert_off(void)
{
    BusScopeOutputStatus status = snapshot();
    assert(pin_mode == GPIO_MODE_OUTPUT_PP && pin_value == GPIO_PIN_RESET);
    assert(!test_tim1.moe && !test_tim1.enabled && !status.applied.enabled);
    assert(status.timing.actual_millihz == 0U && !status.pending);
    assert(!(test_tim1.DIER & TIM_IT_UPDATE) && !(test_tim1.CR1 & TIM_CR1_UDIS));
}

static void assert_applied(uint32_t frequency, uint16_t duty)
{
    BusScopeOutputStatus status = snapshot();
    assert(status.ready && !status.pending && status.applied.enabled);
    assert(status.applied.frequency_hz == frequency && status.applied.duty_permille == duty);
    assert(status.timing.prescaler == test_tim1.active_psc);
    assert(status.timing.autoreload == test_tim1.active_arr);
    assert(status.timing.compare == test_tim1.active_ccr);
}

static void test_initialization(void)
{
    BusScopeOutputConfig config;
    BusScopeOutputStatus status;
    BusScope_OutputDefault(&config);
    assert(config.frequency_step_hz == 10U);
    assert(!BusScope_OutputApply(&config));
    status = snapshot();
    assert(status.error == OUTPUT_ERROR_NOT_INITIALIZED && !status.ready);
    assert(!start_count && !stop_count);
    assert(BusScope_OutputInit());
    assert_off();
    assert(irq_enabled && (test_tim1.CR1 & (TIM_CR1_ARPE | TIM_CR1_URS)) ==
           (TIM_CR1_ARPE | TIM_CR1_URS));
    assert(test_tim1.CCMR2 & TIM_CCMR2_OC3PE);
    assert(BusScope_OutputApply(&config));
    assert_applied(10U, 500U);
    assert(snapshot().timing.actual_millihz == 10000U);
    assert(!(test_tim1.SR & TIM_FLAG_UPDATE) && !irq_pending);
    assert(BusScope_OutputInit());
    assert_applied(10U, 500U);
}

static void test_atomic_update(void)
{
    BusScopeOutputConfig config = snapshot().requested;
    BusScopeOutputStatus status;
    uint32_t starts = start_count, stops = stop_count, events = event_count;
    uint32_t counters = counter_writes, interrupts = irq_count;
    config.frequency_hz = 100000U;
    config.duty_permille = 251U;
    guard_psc = test_tim1.active_psc;
    guard_arr = test_tim1.active_arr;
    guard_ccr = test_tim1.active_ccr;
    inject_overflow = 1U;
    guarded_writes = 0U;
    assert(BusScope_OutputApply(&config));
    inject_overflow = 0U;
    assert(guarded_writes == 3U);
    assert(start_count == starts && stop_count == stops && event_count == events);
    assert(counter_writes == counters && irq_count == interrupts);
    status = snapshot();
    assert(status.ready && status.pending && status.requested.frequency_hz == 100000U);
    assert(status.applied.frequency_hz == 10U && status.timing.prescaler == guard_psc);
    assert(test_tim1.DIER & TIM_IT_UPDATE);
    natural_overflow();
    dispatch_irq();
    assert(!(test_tim1.DIER & TIM_IT_UPDATE) && !(test_tim1.SR & TIM_FLAG_UPDATE));
    assert_applied(100000U, 251U);
    assert(irq_count == interrupts + 1U && !(test_tim1.DIER & TIM_IT_UPDATE));
    natural_overflow();
    dispatch_irq();
    assert(irq_count == interrupts + 1U);

    /* An idle UIF must not acknowledge a newly queued request. */
    assert(test_tim1.SR & TIM_FLAG_UPDATE);
    config.frequency_hz = 12345U;
    assert(BusScope_OutputApply(&config));
    status = snapshot();
    assert(status.pending && status.applied.frequency_hz == 100000U);
    assert(!(test_tim1.SR & TIM_FLAG_UPDATE));
    natural_overflow();
    dispatch_irq();
    assert_applied(12345U, 251U);
}

static void test_replace_and_deferred_irq(void)
{
    BusScopeOutputConfig config = snapshot().requested;
    BusScopeOutputStatus status;
    uint32_t previous = config.frequency_hz;
    config.frequency_hz = 20U;
    assert(BusScope_OutputApply(&config));
    config.frequency_hz = 30U;
    assert(BusScope_OutputApply(&config));
    status = snapshot();
    assert(status.pending && status.requested.frequency_hz == 30U);
    assert(status.applied.frequency_hz == previous);
    {
        uint32_t psc = test_tim1.PSC, arr = test_tim1.ARR, ccr = test_tim1.CCR3;
        uint32_t starts = start_count, stops = stop_count, events = event_count;
        config.frequency_step_hz = 1U;
        assert(BusScope_OutputApply(&config));
        status = snapshot();
        assert(status.pending && status.applied.frequency_hz == previous);
        assert(status.requested.frequency_step_hz == 1U);
        assert(test_tim1.PSC == psc && test_tim1.ARR == arr && test_tim1.CCR3 == ccr);
        assert(start_count == starts && stop_count == stops && event_count == events);
    }
    natural_overflow();
    dispatch_irq();
    assert_applied(30U, 251U);

    config.frequency_hz = 40U;
    assert(BusScope_OutputApply(&config));
    natural_overflow();
    assert(irq_pending);
    /* Apply must retire 40 Hz before publishing the next pending request. */
    config.frequency_hz = 50U;
    assert(BusScope_OutputApply(&config));
    status = snapshot();
    assert(status.pending && status.applied.frequency_hz == 40U);
    assert(status.requested.frequency_hz == 50U);
    assert(status.timing.prescaler == test_tim1.active_psc);
    dispatch_irq();
    assert(snapshot().pending);
    natural_overflow();
    dispatch_irq();
    assert_applied(50U, 251U);

    config.frequency_hz = 60U;
    assert(BusScope_OutputApply(&config));
    natural_overflow();
    /* Snapshot also retires a transfer whose interrupt has not run yet. */
    assert_applied(60U, 251U);
    status = snapshot();
    dispatch_irq();
    assert(snapshot().revision == status.revision);
}

static void test_noop_and_nested_critical(void)
{
    BusScopeOutputConfig config = snapshot().requested;
    uint32_t starts = start_count, stops = stop_count, events = event_count;
    uint32_t counters = counter_writes;
    test_tim1.CNT = 123U;
    test_enter_critical();
    assert(BusScope_OutputApply(&config));
    assert(critical_depth == 1U);
    config.frequency_step_hz = 100U;
    assert(BusScope_OutputApply(&config));
    assert(critical_depth == 1U && snapshot().requested.frequency_step_hz == 100U);
    assert(snapshot().applied.frequency_step_hz == 100U);
    BusScope_OutputSnapshot(NULL);
    assert(critical_depth == 1U);
    test_exit_critical();
    assert(test_tim1.CNT == 123U && !snapshot().pending);
    assert(start_count == starts && stop_count == stops && event_count == events);
    assert(counter_writes == counters);
}

static void test_rejected_requests(void)
{
    BusScopeOutputStatus before = snapshot(), after;
    BusScopeOutputConfig config = before.requested;
    uint32_t starts = start_count, stops = stop_count;
    config.frequency_hz = 0U;
    assert(!BusScope_OutputApply(&config));
    after = snapshot();
    assert(after.error == OUTPUT_ERROR_CONFIG && after.failures == before.failures + 1U);
    assert_applied(before.applied.frequency_hz, before.applied.duty_permille);
    assert(after.requested.frequency_hz == before.requested.frequency_hz);
    assert(!BusScope_OutputApply(NULL));
    config = before.requested;
    pclk = 0U;
    assert(!BusScope_OutputApply(&config) && snapshot().error == OUTPUT_ERROR_CLOCK);
    pclk = 120000000U;
    hclk = 0U;
    assert(!BusScope_OutputApply(&config) && snapshot().error == OUTPUT_ERROR_CLOCK);
    hclk = 240000000U;
    assert(start_count == starts && stop_count == stops);
    assert_applied(before.applied.frequency_hz, before.applied.duty_permille);
    assert(BusScope_OutputApply(&config) && snapshot().error == OUTPUT_ERROR_NONE);

    config.frequency_hz = 70U;
    assert(BusScope_OutputApply(&config));
    config.duty_permille = 1001U;
    assert(!BusScope_OutputApply(&config) && snapshot().pending);
    natural_overflow();
    dispatch_irq();
    assert_applied(70U, before.applied.duty_permille);
}

static void test_stop_and_start_failures(void)
{
    BusScopeOutputConfig config = snapshot().requested;
    BusScopeOutputStatus status;
    config.frequency_hz = 80U;
    assert(BusScope_OutputApply(&config));
    assert(snapshot().pending);
    config.enabled = 0U;
    assert(BusScope_OutputApply(&config));
    natural_overflow();
    dispatch_irq();
    assert_off();
    config.enabled = 1U;
    assert(BusScope_OutputApply(&config));
    config.frequency_hz = 81U;
    assert(BusScope_OutputApply(&config));
    natural_overflow();
    assert(irq_pending);
    config.enabled = 0U;
    assert(BusScope_OutputApply(&config));
    assert_off();
    status = snapshot();
    dispatch_irq();
    assert(snapshot().revision == status.revision && snapshot().ready);
    assert_off();

    config.enabled = 1U;
    fail_pwm = 1U;
    assert(!BusScope_OutputApply(&config));
    assert_off();
    status = snapshot();
    assert(!status.ready && status.error == OUTPUT_ERROR_START && status.requested.enabled);
    fail_pwm = 0U;
    fail_event = 1U;
    assert(!BusScope_OutputApply(&config));
    assert_off();
    assert(snapshot().error == OUTPUT_ERROR_START);
    fail_event = 0U;
    assert(BusScope_OutputApply(&config));
    assert_applied(81U, config.duty_permille);
    assert(snapshot().error == OUTPUT_ERROR_NONE);
}

static void test_endpoints_and_clock_modes(void)
{
    BusScopeOutputConfig config = snapshot().requested;
    uint32_t starts = start_count, stops = stop_count;
    config.frequency_hz = 1U;
    config.duty_permille = 1000U;
    assert(BusScope_OutputApply(&config));
    natural_overflow();
    dispatch_irq();
    assert_applied(1U, 1000U);
    assert(test_tim1.active_ccr == test_tim1.active_arr + 1U);
    assert(test_tim1.active_ccr <= UINT16_MAX);
    config.duty_permille = 0U;
    assert(BusScope_OutputApply(&config));
    natural_overflow();
    dispatch_irq();
    assert_applied(1U, 0U);
    assert(test_tim1.active_ccr == 0U);
    assert(start_count == starts && stop_count == stops);

    for (unsigned mode = 0U; mode < 4U; mode++)
    {
        uint64_t expected_clock = (mode == 0U || mode == 2U) ? 240000000U : 120000000U;
        uint64_t divider;
        config.enabled = 0U;
        assert(BusScope_OutputApply(&config));
        RCC->CFGR = mode >= 2U ? RCC_CFGR_TIMPRE : 0U;
        pclk = mode == 0U ? hclk : mode == 1U ? 60000000U :
               mode == 2U ? 60000000U : 30000000U;
        config.frequency_hz = 10U;
        config.duty_permille = 500U;
        config.enabled = 1U;
        assert(BusScope_OutputApply(&config));
        assert_applied(10U, 500U);
        assert(snapshot().timing.actual_millihz == 10000U);
        divider = ((uint64_t)test_tim1.active_psc + 1U) * (test_tim1.active_arr + 1U);
        assert(divider * 10U == expected_clock);
    }
}

int main(void)
{
    test_initialization();
    test_atomic_update();
    test_replace_and_deferred_irq();
    test_noop_and_nested_critical();
    test_rejected_requests();
    test_stop_and_start_failures();
    test_endpoints_and_clock_modes();
    assert(critical_depth == 0U);
    puts("bus_scope_output_hw: all tests passed");
    return 0;
}
