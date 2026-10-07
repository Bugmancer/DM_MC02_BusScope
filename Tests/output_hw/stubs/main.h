#ifndef TEST_OUTPUT_HW_MAIN_H
#define TEST_OUTPUT_HW_MAIN_H
#include <stdint.h>
#define HAL_OK 0U
#define HAL_ERROR 1U
#define TIM_CHANNEL_3 3U
#define TIM_EVENTSOURCE_UPDATE 1U
#define TIM_CR1_UDIS 2U
#define TIM_CR1_URS 4U
#define TIM_CR1_ARPE 128U
#define TIM_CCMR2_OC3PE 8U
#define TIM_FLAG_UPDATE 1U
#define TIM_IT_UPDATE 1U
#define TIM1_UP_IRQn 25U
#define RCC_CFGR_TIMPRE 1U
#define GPIO_PIN_13 8192U
#define GPIO_PIN_RESET 0U
#define GPIO_MODE_AF_PP 1U
#define GPIO_MODE_OUTPUT_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 3U
#define GPIO_AF1_TIM1 1U
#define GPIOE ((void *)1)
typedef struct
{
    uint32_t PSC, ARR, CNT, CCR3, CR1, CCMR2, DIER, SR, enabled, moe;
    uint32_t active_psc, active_arr, active_ccr;
} TestTimer;
extern TestTimer test_tim1;
typedef struct { TestTimer *Instance; } TIM_HandleTypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef struct { uint32_t CFGR; } TestRcc;
extern TestRcc *RCC;
extern TIM_HandleTypeDef htim1;
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define SET_BIT(reg, bits) ((reg) |= (bits))
#define CLEAR_BIT(reg, bits) ((reg) &= ~(bits))
#define __HAL_TIM_SET_PRESCALER(h, v) test_write_prescaler((h), (v))
#define __HAL_TIM_SET_AUTORELOAD(h, v) test_write_autoreload((h), (v))
#define __HAL_TIM_SET_COUNTER(h, v) test_write_counter((h), (v))
#define __HAL_TIM_SET_COMPARE(h, ch, v) test_write_compare((h), (ch), (v))
#define __HAL_TIM_ENABLE_OCxPRELOAD(h, ch) test_enable_preload((h), (ch))
#define __HAL_TIM_GET_FLAG(h, flag) (((h)->Instance->SR & (flag)) != 0U)
#define __HAL_TIM_CLEAR_FLAG(h, flag) ((h)->Instance->SR &= ~(flag))
#define __HAL_TIM_GET_IT_SOURCE(h, it) (((h)->Instance->DIER & (it)) != 0U)
#define __HAL_TIM_ENABLE_IT(h, it) test_enable_interrupt((h), (it))
#define __HAL_TIM_DISABLE_IT(h, it) ((h)->Instance->DIER &= ~(it))
void test_write_prescaler(TIM_HandleTypeDef *, uint32_t);
void test_write_autoreload(TIM_HandleTypeDef *, uint32_t);
void test_write_counter(TIM_HandleTypeDef *, uint32_t);
void test_write_compare(TIM_HandleTypeDef *, uint32_t, uint32_t);
void test_enable_preload(TIM_HandleTypeDef *, uint32_t);
void test_enable_interrupt(TIM_HandleTypeDef *, uint32_t);
uint32_t HAL_RCC_GetPCLK2Freq(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
void HAL_GPIO_WritePin(void *, uint32_t, uint32_t);
void HAL_GPIO_Init(void *, GPIO_InitTypeDef *);
uint32_t HAL_TIM_PWM_Stop(TIM_HandleTypeDef *, uint32_t);
uint32_t HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t);
uint32_t HAL_TIM_GenerateEvent(TIM_HandleTypeDef *, uint32_t);
void HAL_NVIC_SetPriority(uint32_t, uint32_t, uint32_t);
void HAL_NVIC_ClearPendingIRQ(uint32_t);
void HAL_NVIC_EnableIRQ(uint32_t);
#endif
