#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
typedef int BaseType_t;
#define pdFALSE 0
#define taskENTER_CRITICAL() ((void)0)
#define taskEXIT_CRITICAL() ((void)0)
#define portYIELD_FROM_ISR(wake) ((void)(wake))
#endif
