#ifndef TICK_HW_H
#define TICK_HW_H

#include "../core/infra_types.h"

typedef void (*TickHwHandler)(void);

InfraStatus TickHw_Init(TickHwHandler handler);
void        TickHw_ClearCompareFlag(void);

#endif
