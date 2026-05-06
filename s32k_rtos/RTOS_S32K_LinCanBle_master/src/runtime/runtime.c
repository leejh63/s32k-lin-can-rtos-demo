// master 노드 전용 runtime 구현 파일이다.
// coordinator가 실제로 쓰는 task만 등록하여,
// 다른 노드용 no-op task 없이 필요한 FreeRTOS task만 구성한다.
#include <stdint.h>

#include "runtime.h"

#include "FreeRTOS.h"
#include "portable.h"
#include "task.h"

#include "../app/app_config.h"
#include "../app/app_core_internal.h"
#include "../core/runtime_tick.h"
#include "runtime_io.h"

typedef struct
{
    uint8_t     initialized;
    InfraStatus init_status;
    AppCore     app;
    RuntimeObservabilitySnapshot observability;
    TaskHandle_t uart_task_handle;
    TaskHandle_t heartbeat_task_handle;
    TaskHandle_t can_task_handle;
    TaskHandle_t lin_task_handle;
    TaskHandle_t render_task_handle;
} RuntimeContext;

static RuntimeContext g_runtime;

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
#define RUNTIME_SRAM_L_END_ADDR   (0x20000000u)

extern uint8_t __code_end__;

static void Runtime_DefineHeap5Regions(void)
{
    static uint8_t heap_regions_defined = 0U;
    HeapRegion_t   heap_regions[2];
    uintptr_t      heap_start_addr;
    size_t         heap_size;

    if (heap_regions_defined != 0U)
    {
        return;
    }

    heap_start_addr = (uintptr_t)&__code_end__;
    heap_start_addr = (heap_start_addr + 7u) & ~(uintptr_t)7u;

    configASSERT(heap_start_addr < (uintptr_t)RUNTIME_SRAM_L_END_ADDR);

    heap_size = (size_t)((uintptr_t)RUNTIME_SRAM_L_END_ADDR - heap_start_addr);
    configASSERT(heap_size > 0u);

    heap_regions[0].pucStartAddress = (uint8_t *)heap_start_addr;
    heap_regions[0].xSizeInBytes    = heap_size;
    heap_regions[1].pucStartAddress = NULL;
    heap_regions[1].xSizeInBytes    = 0u;

    vPortDefineHeapRegions(heap_regions);
    heap_regions_defined = 1U;
}
#endif

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )
static StaticTask_t g_idle_task_tcb;
static StackType_t  g_idle_task_stack[configMINIMAL_STACK_SIZE];

#if ( configUSE_TIMERS == 1 )
static StaticTask_t g_timer_task_tcb;
static StackType_t  g_timer_task_stack[configTIMER_TASK_STACK_DEPTH];
#endif

static StaticTask_t g_can_task_tcb;
static StackType_t  g_can_task_stack[APP_RTOS_CAN_TASK_STACK_WORDS];

void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    if ((ppxIdleTaskTCBBuffer == NULL) ||
        (ppxIdleTaskStackBuffer == NULL) ||
        (pulIdleTaskStackSize == NULL))
    {
        return;
    }

    *ppxIdleTaskTCBBuffer = &g_idle_task_tcb;
    *ppxIdleTaskStackBuffer = g_idle_task_stack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

#if ( configUSE_TIMERS == 1 )
void vApplicationGetTimerTaskMemory(StaticTask_t **ppxTimerTaskTCBBuffer,
                                    StackType_t **ppxTimerTaskStackBuffer,
                                    uint32_t *pulTimerTaskStackSize)
{
    if ((ppxTimerTaskTCBBuffer == NULL) ||
        (ppxTimerTaskStackBuffer == NULL) ||
        (pulTimerTaskStackSize == NULL))
    {
        return;
    }

    *ppxTimerTaskTCBBuffer = &g_timer_task_tcb;
    *ppxTimerTaskStackBuffer = g_timer_task_stack;
    *pulTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif
#endif

static void Runtime_EnterObservabilityCritical(void)
{
    taskENTER_CRITICAL();
}

static void Runtime_ExitObservabilityCritical(void)
{
    taskEXIT_CRITICAL();
}

static uint32_t Runtime_GetTaskStackHighWaterWords(TaskHandle_t task_handle)
{
    if (task_handle == NULL)
    {
        return 0U;
    }

    return (uint32_t)uxTaskGetStackHighWaterMark(task_handle);
}

// task 문맥에서 app logic에 넘길 now_ms는 FreeRTOS tick 기준으로만 계산한다.
// LIN timeout service 같은 ISR 경로는 RuntimeTick이 별도로 맡는다.
static uint32_t Runtime_GetTaskContextNowMs(void)
{
    TickType_t now_ticks;

    now_ticks = xTaskGetTickCount();
    return (uint32_t)((((uint64_t)now_ticks) * 1000ULL) / (uint64_t)configTICK_RATE_HZ);
}

static void Runtime_UpdateObservability(uint32_t now_ms)
{
    RuntimeObservabilitySnapshot snapshot;

    if ((g_runtime.observability.sample_count != 0U) &&
        (Infra_TimeIsDue(now_ms,
                         g_runtime.observability.last_update_ms,
                         APP_RTOS_OBSERVABILITY_PERIOD_MS) == 0U))
    {
        return;
    }

    snapshot.uart_stack_hwm_words = Runtime_GetTaskStackHighWaterWords(g_runtime.uart_task_handle);
    snapshot.hb_stack_hwm_words = Runtime_GetTaskStackHighWaterWords(g_runtime.heartbeat_task_handle);
    snapshot.can_stack_hwm_words = Runtime_GetTaskStackHighWaterWords(g_runtime.can_task_handle);
    snapshot.lin_stack_hwm_words = Runtime_GetTaskStackHighWaterWords(g_runtime.lin_task_handle);
    snapshot.render_stack_hwm_words = Runtime_GetTaskStackHighWaterWords(g_runtime.render_task_handle);
    snapshot.free_heap_bytes = (uint32_t)xPortGetFreeHeapSize();
    snapshot.min_ever_free_heap_bytes = (uint32_t)xPortGetMinimumEverFreeHeapSize();

    Runtime_EnterObservabilityCritical();
    snapshot.sample_count = g_runtime.observability.sample_count + 1U;
    snapshot.last_update_ms = now_ms;
    g_runtime.observability = snapshot;
    Runtime_ExitObservabilityCritical();
}

// heartbeat만 dispatcher 밖의 native FreeRTOS task로 분리한 wrapper다.
// 기존 AppCore 로직은 그대로 재사용하고,
// 주기 보장은 FreeRTOS의 vTaskDelayUntil()에 맡긴다.
static void Runtime_HeartbeatTask(void *argument)
{
    TickType_t heartbeat_ticks;
    TickType_t last_wake_tick;
    uint32_t   now_ms;

    (void)argument;

    heartbeat_ticks = pdMS_TO_TICKS(APP_RTOS_HEARTBEAT_PERIOD_MS);
    if (heartbeat_ticks == 0U)
    {
        heartbeat_ticks = 1U;
    }

    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_ms = Runtime_GetTaskContextNowMs();
        Runtime_UpdateObservability(now_ms);
        AppCore_TaskHeartbeat(&g_runtime.app, now_ms);
        vTaskDelayUntil(&last_wake_tick, heartbeat_ticks);
    }
}

// mailbox hardening 이후 guarded split으로 CAN만 dispatcher 밖에 둔다.
// AppCore CAN 로직은 그대로 재사용하고 주기만 native task가 맡는다.
static void Runtime_CanTask(void *argument)
{
    TickType_t can_ticks;
    TickType_t last_wake_tick;
    uint32_t   now_ms;

    (void)argument;

    can_ticks = pdMS_TO_TICKS(APP_RTOS_CAN_PERIOD_MS);
    if (can_ticks == 0U)
    {
        can_ticks = 1U;
    }

    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_ms = Runtime_GetTaskContextNowMs();
        AppCore_TaskCan(&g_runtime.app, now_ms);
        vTaskDelayUntil(&last_wake_tick, can_ticks);
    }
}

// render input/output owner 정리 이후 render도 dispatcher 밖으로 분리한다.
// 기존 AppCore render 로직은 그대로 두고 주기만 native task로 옮긴다.
static void Runtime_RenderTask(void *argument)
{
    TickType_t render_ticks;
    TickType_t last_wake_tick;
    uint32_t   now_ms;

    (void)argument;

    render_ticks = pdMS_TO_TICKS(APP_RTOS_RENDER_PERIOD_MS);
    if (render_ticks == 0U)
    {
        render_ticks = 1U;
    }

    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_ms = Runtime_GetTaskContextNowMs();
        AppCore_TaskRender(&g_runtime.app, now_ms);
        vTaskDelayUntil(&last_wake_tick, render_ticks);
    }
}

// UART owner는 모든 최종 console/ble commit을 이미 맡고 있으므로,
// dispatcher 없이도 1ms native task로 직접 돌릴 수 있다.
static void Runtime_UartTask(void *argument)
{
    TickType_t uart_ticks;
    TickType_t last_wake_tick;
    uint32_t   now_ms;

    (void)argument;

    uart_ticks = pdMS_TO_TICKS(APP_RTOS_UART_PERIOD_MS);
    if (uart_ticks == 0U)
    {
        uart_ticks = 1U;
    }

    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_ms = Runtime_GetTaskContextNowMs();
        AppCore_TaskUart(&g_runtime.app, now_ms);
        vTaskDelayUntil(&last_wake_tick, uart_ticks);
    }
}

// LIN owner 경계를 정리한 뒤에는 fast/poll을 한 task 문맥에서만 순차 실행한다.
static void Runtime_LinTask(void *argument)
{
    TickType_t lin_ticks;
    TickType_t last_wake_tick;
    uint32_t   last_poll_ms;

    (void)argument;

    lin_ticks = pdMS_TO_TICKS(APP_RTOS_LIN_TASK_PERIOD_MS);
    if (lin_ticks == 0U)
    {
        lin_ticks = 1U;
    }

    last_wake_tick = xTaskGetTickCount();
    last_poll_ms = Runtime_GetTaskContextNowMs();

    for (;;)
    {
        uint32_t now_ms;

        now_ms = Runtime_GetTaskContextNowMs();
        AppCore_TaskLinFast(&g_runtime.app, now_ms);

        if (Infra_TimeIsDue(now_ms, last_poll_ms, APP_RTOS_LIN_POLL_PERIOD_MS) != 0U)
        {
            AppCore_TaskLinPoll(&g_runtime.app, now_ms);
            last_poll_ms = now_ms;
        }

        vTaskDelayUntil(&last_wake_tick, lin_ticks);
    }
}

// 초기화 실패 시 빠져나오지 않는 fault loop다.
static void Runtime_FaultLoop(void)
{
    for (;;)
    {
    }
}

// runtime, 보드, tick, app, ISR hook를 차례대로 준비한다.
// 어느 한 단계라도 실패하면 이후 run 단계로 가지 않게 막는다.
InfraStatus Runtime_Init(void)
{
    InfraStatus status;

    g_runtime.initialized = 0U;
    g_runtime.init_status = INFRA_STATUS_NOT_READY;
    g_runtime.observability = (RuntimeObservabilitySnapshot){0};
    g_runtime.uart_task_handle = NULL;
    g_runtime.heartbeat_task_handle = NULL;
    g_runtime.can_task_handle = NULL;
    g_runtime.lin_task_handle = NULL;
    g_runtime.render_task_handle = NULL;

    status = RuntimeIo_BoardInit();
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    status = RuntimeTick_Init();
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    status = AppCore_Init(&g_runtime.app);
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    RuntimeTick_ClearHooks();
    status = RuntimeTick_RegisterHook(AppCore_OnTickIsr, &g_runtime.app);
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    g_runtime.initialized = 1U;
    g_runtime.init_status = INFRA_STATUS_OK;
    return INFRA_STATUS_OK;
}

// 모든 주기 기능을 native task로 직접 만들고 scheduler를 시작한다.
void Runtime_Run(void)
{
    BaseType_t uart_create_status;
    BaseType_t heartbeat_create_status;
    BaseType_t lin_create_status;
    BaseType_t render_create_status;

    if ((g_runtime.initialized == 0U) || (g_runtime.init_status != INFRA_STATUS_OK))
    {
        Runtime_FaultLoop();
    }

#if ( configSUPPORT_DYNAMIC_ALLOCATION == 1 )
    Runtime_DefineHeap5Regions();
#endif

    uart_create_status = xTaskCreate(Runtime_UartTask,
                                     "app_uart",
                                     APP_RTOS_UART_TASK_STACK_WORDS,
                                     NULL,
                                     APP_RTOS_UART_TASK_PRIORITY,
                                     &g_runtime.uart_task_handle);
    if (uart_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    heartbeat_create_status = xTaskCreate(Runtime_HeartbeatTask,
                                          "app_hb",
                                          APP_RTOS_HEARTBEAT_TASK_STACK_WORDS,
                                          NULL,
                                          APP_RTOS_HEARTBEAT_TASK_PRIORITY,
                                          &g_runtime.heartbeat_task_handle);
    if (heartbeat_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    g_runtime.can_task_handle = xTaskCreateStatic(Runtime_CanTask,
                                                  "app_can",
                                                  APP_RTOS_CAN_TASK_STACK_WORDS,
                                                  NULL,
                                                  APP_RTOS_CAN_TASK_PRIORITY,
                                                  g_can_task_stack,
                                                  &g_can_task_tcb);
    if (g_runtime.can_task_handle == NULL)
    {
        Runtime_FaultLoop();
    }

    lin_create_status = xTaskCreate(Runtime_LinTask,
                                    "app_lin",
                                    APP_RTOS_LIN_TASK_STACK_WORDS,
                                    NULL,
                                    APP_RTOS_LIN_TASK_PRIORITY,
                                    &g_runtime.lin_task_handle);
    if (lin_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    render_create_status = xTaskCreate(Runtime_RenderTask,
                                       "app_render",
                                       APP_RTOS_RENDER_TASK_STACK_WORDS,
                                       NULL,
                                       APP_RTOS_RENDER_TASK_PRIORITY,
                                       &g_runtime.render_task_handle);
    if (render_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    vTaskStartScheduler();
    Runtime_FaultLoop();
}

// 현재 runtime이 들고 있는 AppCore를 조회한다.
const AppCore *Runtime_GetApp(void)
{
    return &g_runtime.app;
}

InfraStatus Runtime_GetObservabilitySnapshot(RuntimeObservabilitySnapshot *out_snapshot)
{
    if (out_snapshot == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if ((g_runtime.initialized == 0U) || (g_runtime.init_status != INFRA_STATUS_OK))
    {
        return INFRA_STATUS_NOT_READY;
    }

    Runtime_EnterObservabilityCritical();
    *out_snapshot = g_runtime.observability;
    Runtime_ExitObservabilityCritical();

    if (out_snapshot->sample_count == 0U)
    {
        return INFRA_STATUS_EMPTY;
    }

    return INFRA_STATUS_OK;
}

// 참고:
// fault loop가 아주 단순해서 bring-up 중 실패 원인을 현장에서 바로 알기는 어렵다.
// 확장 시에는 마지막 init 실패 코드를 LED나 콘솔로 남기는 정도만 더해도 도움이 된다.
