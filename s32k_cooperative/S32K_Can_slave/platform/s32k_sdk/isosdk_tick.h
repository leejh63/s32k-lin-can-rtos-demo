#ifndef ISOSDK_TICK_H
#define ISOSDK_TICK_H

#include <stdint.h>

typedef void (*IsoSdkTickHandler)(void);

uint8_t IsoSdk_TickInit(IsoSdkTickHandler handler);
void    IsoSdk_TickClearCompareFlag(void);

#endif
