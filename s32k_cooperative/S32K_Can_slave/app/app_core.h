// CAN 현장 반응 slave 최소 운영 구성용 App public API다.
// concrete AppCore 상태는 내부 헤더에 숨기고,
// runtime과 상위 계층에는 초기화와 task 진입점만 노출한다.
#ifndef APP_CORE_H
#define APP_CORE_H

#include <stdint.h>

#include "../core/infra_types.h"

typedef struct AppCore AppCore;

typedef struct
{
    uint8_t  local_node_id;
    uint32_t can_default_timeout_ms;
    uint8_t  can_max_submit_per_tick;
} AppCoreConfig;

InfraStatus AppCore_Init(AppCore *app, const AppCoreConfig *config);
void        AppCore_TaskHeartbeat(AppCore *app, uint32_t now_ms);
void        AppCore_TaskCan(AppCore *app, uint32_t now_ms);
void        AppCore_TaskButton(AppCore *app, uint32_t now_ms);
void        AppCore_TaskLed(AppCore *app, uint32_t now_ms);

#endif
