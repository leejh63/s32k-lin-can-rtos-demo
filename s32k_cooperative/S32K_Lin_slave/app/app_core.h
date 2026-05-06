#ifndef APP_CORE_H
#define APP_CORE_H

#include "../core/infra_types.h"

typedef struct AppCore AppCore;

InfraStatus AppCore_Init(AppCore *app);
void        AppCore_OnTickIsr(void *context);
void        AppCore_TaskHeartbeat(AppCore *app, uint32_t now_ms);
void        AppCore_TaskLinFast(AppCore *app, uint32_t now_ms);
void        AppCore_TaskLed(AppCore *app, uint32_t now_ms);
void        AppCore_TaskAdc(AppCore *app, uint32_t now_ms);

#endif
