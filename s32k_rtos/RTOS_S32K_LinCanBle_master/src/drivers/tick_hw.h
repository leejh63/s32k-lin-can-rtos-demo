#ifndef TICK_HW_H
#define TICK_HW_H

#include "../core/infra_types.h"

typedef void (*TickHwHandler)(void);

// TickHw를 초기화한다.
InfraStatus TickHw_Init(TickHwHandler handler);
void        TickHw_ClearCompareFlag(void);

#endif
