#ifndef BUS_SCOPE_H
#define BUS_SCOPE_H

#include "main.h"

void BusScope_Init(void);
void BusScope_KeyTask(void const *argument);
void BusScope_LcdTask(void const *argument);
void BusScope_CanTask(void const *argument);
void BusScope_SampleTask(void const *argument);
void BusScope_UsbTask(void const *argument);

#endif
