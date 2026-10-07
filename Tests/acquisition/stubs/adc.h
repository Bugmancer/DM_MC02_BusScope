#ifndef TEST_ADC_H
#define TEST_ADC_H
#include <stdint.h>
typedef struct { uint32_t remaining, flags; } DMA_HandleTypeDef;
typedef struct { uint32_t ErrorCode; DMA_HandleTypeDef *DMA_Handle; } ADC_HandleTypeDef;
typedef struct { uint32_t CR1, CR2, DIER, PSC, ARR, CNT, EGR, SR; } TIM_TypeDef;
typedef struct { uint32_t CFGR; } RCC_TypeDef;
typedef struct { uint32_t CCR; } SCB_Type;
extern ADC_HandleTypeDef hadc1;
extern TIM_TypeDef test_tim6;
extern RCC_TypeDef test_rcc;
extern SCB_Type test_scb;
#define TIM6 (&test_tim6)
#define RCC (&test_rcc)
#define SCB (&test_scb)
#define RCC_CFGR_TIMPRE 1U
#define TIM_CR1_CEN 1U
#define TIM_CR1_ARPE 128U
#define TIM_EGR_UG 1U
#define TIM_TRGO_UPDATE 32U
#define SCB_CCR_DC_Msk 1U
#define HAL_OK 0U
#define HAL_ERROR 1U
#define ADC_CALIB_OFFSET_LINEARITY 1U
#define ADC_SINGLE_ENDED 0U
#define __HAL_RCC_D2SRAM1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM6_CLK_ENABLE() ((void)0)
#define __HAL_RCC_TIM6_FORCE_RESET() ((void)0)
#define __HAL_RCC_TIM6_RELEASE_RESET() ((void)0)
#define __HAL_DMA_GET_COUNTER(dma) ((dma)->remaining)
#define __HAL_DMA_GET_TC_FLAG_INDEX(dma) ((void)(dma), 1U)
#define __HAL_DMA_GET_HT_FLAG_INDEX(dma) ((void)(dma), 2U)
#define __HAL_DMA_GET_FLAG(dma, flag) ((dma)->flags & (flag))
#define __DMB() ((void)0)
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
uint32_t HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *, uint32_t, uint32_t);
uint32_t HAL_ADC_Start_DMA(ADC_HandleTypeDef *, uint32_t *, uint32_t);
uint32_t HAL_ADC_Stop_DMA(ADC_HandleTypeDef *);
void SCB_CleanInvalidateDCache_by_Addr(uint32_t *, int32_t);
void SCB_InvalidateDCache_by_Addr(uint32_t *, int32_t);
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *);
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *);
#endif
