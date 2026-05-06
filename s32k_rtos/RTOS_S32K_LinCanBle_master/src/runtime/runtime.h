// 프로젝트의 최상위 runtime 인터페이스다.
// 상위 코드는 이 함수들만 호출하면 시스템을 초기화하고,
// FreeRTOS task 기반 runtime으로 진입할 수 있다.
#ifndef RUNTIME_H
#define RUNTIME_H

#include <stdint.h>

#include "../app/app_core.h"
#include "../core/infra_types.h"

typedef struct
{
    uint32_t uart_stack_hwm_words;
    uint32_t hb_stack_hwm_words;
    uint32_t can_stack_hwm_words;
    uint32_t lin_stack_hwm_words;
    uint32_t render_stack_hwm_words;
    uint32_t free_heap_bytes;
    uint32_t min_ever_free_heap_bytes;
    uint32_t sample_count;
    uint32_t last_update_ms;
} RuntimeObservabilitySnapshot;

// Runtime를 초기화한다.
InfraStatus    Runtime_Init(void);
// Runtime 실행 루프를 시작한다.
void           Runtime_Run(void);
// 현재 runtime이 보유한 AppCore를 조회한다.
const AppCore *Runtime_GetApp(void);
// 최신 runtime 관측 snapshot을 조회한다.
InfraStatus    Runtime_GetObservabilitySnapshot(RuntimeObservabilitySnapshot *out_snapshot);

#endif
