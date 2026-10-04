#ifndef TEST_APP_MAIN_H
#define TEST_APP_MAIN_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

/* Only the HAL/RTOS surface referenced by bus_scope.c is modeled here. */
#define HAL_OK 0U
#define ADC_CALIB_OFFSET 0U
#define ADC_SINGLE_ENDED 0U
#define DMA_IT_HT 1U
#define DMA_IT_TC 2U
#define TIM_CHANNEL_3 3U
#define TIM_EVENTSOURCE_UPDATE 1U
#define SCB_CCR_DC_Msk (1UL << 16)
#define USBD_OK 0U

typedef struct { void *DMA_Handle; } ADC_HandleTypeDef;
typedef struct { uint32_t unused; } TIM_HandleTypeDef;
typedef struct { uint32_t CCR; } TestSCB;
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim1;
extern TestSCB *SCB;
uint32_t HAL_GetTick(void);
uint32_t HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *, uint32_t, uint32_t);
uint32_t HAL_ADC_Start_DMA(ADC_HandleTypeDef *, uint32_t *, uint32_t);
uint32_t HAL_TIM_GenerateEvent(TIM_HandleTypeDef *, uint32_t);
uint32_t HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t);
void SCB_InvalidateDCache_by_Addr(uint32_t *, int32_t);
void SCB_CleanInvalidateDCache_by_Addr(uint32_t *, int32_t);
#define __HAL_RCC_D2SRAM1_CLK_ENABLE() ((void)0)
#define __HAL_DMA_DISABLE_IT(handle, flags) ((void)(handle), (void)(flags))
#define __HAL_TIM_SET_PRESCALER(handle, value) ((void)(handle), (void)(value))
#define __HAL_TIM_SET_AUTORELOAD(handle, value) ((void)(handle), (void)(value))
#define __HAL_TIM_SET_COMPARE(handle, channel, value) ((void)(handle), (void)(channel), (void)(value))

typedef struct { void *Instance; } FDCAN_HandleTypeDef;
typedef struct
{
    uint32_t Identifier;
    uint32_t DataLength;
    uint32_t FDFormat;
    uint32_t BitRateSwitch;
    uint32_t IdType;
} FDCAN_RxHeaderTypeDef;
typedef struct
{
    uint32_t IdType;
    uint32_t FilterIndex;
    uint32_t FilterType;
    uint32_t FilterConfig;
    uint32_t FilterID1;
    uint32_t FilterID2;
} FDCAN_FilterTypeDef;
typedef struct { uint32_t BusOff; } FDCAN_ProtocolStatusTypeDef;
extern FDCAN_HandleTypeDef hfdcan1, hfdcan2, hfdcan3;
#define FDCAN1 ((void *)(uintptr_t)1U)
#define FDCAN2 ((void *)(uintptr_t)2U)
#define FDCAN3 ((void *)(uintptr_t)3U)
#define FDCAN_RX_FIFO0 0U
#define FDCAN_STANDARD_ID 0U
#define FDCAN_EXTENDED_ID 0x40000000U
#define FDCAN_CLASSIC_CAN 0U
#define FDCAN_FD_CAN 0x00200000U
#define FDCAN_BRS_ON 0x00100000U
#define FDCAN_FILTER_MASK 0U
#define FDCAN_FILTER_TO_RXFIFO0 0U
#define FDCAN_ACCEPT_IN_RX_FIFO0 0U
#define FDCAN_REJECT_REMOTE 0U
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE 1U
#define FDCAN_IT_RX_FIFO0_MESSAGE_LOST 2U
#define FDCAN_DLC_BYTES_0  0x00000000U
#define FDCAN_DLC_BYTES_1  0x00010000U
#define FDCAN_DLC_BYTES_2  0x00020000U
#define FDCAN_DLC_BYTES_3  0x00030000U
#define FDCAN_DLC_BYTES_4  0x00040000U
#define FDCAN_DLC_BYTES_5  0x00050000U
#define FDCAN_DLC_BYTES_6  0x00060000U
#define FDCAN_DLC_BYTES_7  0x00070000U
#define FDCAN_DLC_BYTES_8  0x00080000U
#define FDCAN_DLC_BYTES_12 0x00090000U
#define FDCAN_DLC_BYTES_16 0x000a0000U
#define FDCAN_DLC_BYTES_20 0x000b0000U
#define FDCAN_DLC_BYTES_24 0x000c0000U
#define FDCAN_DLC_BYTES_32 0x000d0000U
#define FDCAN_DLC_BYTES_48 0x000e0000U
#define FDCAN_DLC_BYTES_64 0x000f0000U
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *, uint32_t);
uint32_t HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *, uint32_t, FDCAN_RxHeaderTypeDef *, uint8_t *);
uint32_t HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *, FDCAN_FilterTypeDef *);
uint32_t HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t HAL_FDCAN_Start(FDCAN_HandleTypeDef *);
uint32_t HAL_FDCAN_Stop(FDCAN_HandleTypeDef *);
uint32_t HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *, uint32_t, uint32_t);
uint32_t HAL_FDCAN_GetProtocolStatus(FDCAN_HandleTypeDef *, FDCAN_ProtocolStatusTypeDef *);

#define LCD_W 280U
#define LCD_H 240U
#define BLACK 0U
#define RED 1U
#define GREEN 2U
#define WHITE 3U
#define CYAN 4U
#define GRAYBLUE 5U
#define DARKBLUE 6U
#define YELLOW 7U
void LCD_Init(void);
void LCD_Fill(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);
void LCD_ShowString(uint16_t, uint16_t, const uint8_t *, uint16_t, uint16_t, uint8_t, uint8_t);
void LCD_DrawRectangle(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);
void LCD_DrawLine(uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);
void LCD_DrawPoint(uint16_t, uint16_t, uint16_t);
uint8_t CDC_Transmit_HS(uint8_t *, uint16_t);

typedef uint32_t TickType_t;
typedef int32_t BaseType_t;
typedef void *TaskHandle_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(value) ((TickType_t)(value))
#define configASSERT(condition) assert(condition)
extern uint32_t test_critical_depth;
#define taskENTER_CRITICAL() (++test_critical_depth)
#define taskEXIT_CRITICAL() (--test_critical_depth)
#define portYIELD_FROM_ISR(value) ((void)(value))
void osDelay(uint32_t);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
TickType_t xTaskGetTickCount(void);
void vTaskDelayUntil(TickType_t *, TickType_t);
uint32_t ulTaskNotifyTake(BaseType_t, TickType_t);
void vTaskNotifyGiveFromISR(TaskHandle_t, BaseType_t *);

#endif
