#ifndef TEST_OUTPUT_HW_MAIN_H
#define TEST_OUTPUT_HW_MAIN_H
#include <stdint.h>
#define HAL_OK 0U
#define HAL_ERROR 1U
#define TIM_CHANNEL_3 3U
#define TIM_EVENTSOURCE_UPDATE 1U
#define RCC_CFGR_TIMPRE 1U
#define GPIO_PIN_13 8192U
#define GPIO_PIN_RESET 0U
#define GPIO_MODE_AF_PP 1U
#define GPIO_MODE_OUTPUT_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 3U
#define GPIO_AF1_TIM1 1U
#define GPIOE ((void *)1)
typedef struct { uint32_t PSC, ARR, CNT, CCR3, enabled, moe; } TestTimer;
extern TestTimer test_tim1;
typedef struct { TestTimer *Instance; } TIM_HandleTypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef struct { uint32_t CFGR; } TestRcc;
extern TestRcc *RCC;
extern TIM_HandleTypeDef htim1;
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define __HAL_TIM_SET_PRESCALER(h, v) ((h)->Instance->PSC = (v))
#define __HAL_TIM_SET_AUTORELOAD(h, v) ((h)->Instance->ARR = (v))
#define __HAL_TIM_SET_COUNTER(h, v) ((h)->Instance->CNT = (v))
#define __HAL_TIM_SET_COMPARE(h, ch, v) ((void)(ch), (h)->Instance->CCR3 = (v))
uint32_t HAL_RCC_GetPCLK2Freq(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
void HAL_GPIO_WritePin(void *, uint32_t, uint32_t);
void HAL_GPIO_Init(void *, GPIO_InitTypeDef *);
uint32_t HAL_TIM_PWM_Stop(TIM_HandleTypeDef *, uint32_t);
uint32_t HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t);
uint32_t HAL_TIM_GenerateEvent(TIM_HandleTypeDef *, uint32_t);
#endif
