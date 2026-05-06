// CAN 현장 반응 slave 최소 운영 구성용 runtime 구현 파일이다.
// slave1이 실제로 쓰는 heartbeat, button, led, can task를
// native FreeRTOS task로 구성하고, 보드 초기화와 scheduler 구성을 runtime에만 남긴다.
#include "runtime.h"

#include "FreeRTOS.h"
#include "task.h"

#include "../app/app_core_internal.h"
#include "../app/app_config.h"
#include "../drivers/board_hw.h"

typedef struct
{
    uint8_t          initialized;
    InfraStatus      init_status;
    volatile int32_t rtos_heartbeat_create_status;
    volatile int32_t rtos_button_create_status;
    volatile int32_t rtos_led_create_status;
    volatile int32_t rtos_can_create_status;
    volatile uint8_t rtos_scheduler_start_called;
    volatile uint8_t rtos_scheduler_returned;
    volatile uint8_t rtos_heartbeat_entered;
    volatile uint8_t rtos_button_entered;
    volatile uint8_t rtos_led_entered;
    volatile uint8_t rtos_can_entered;
    volatile uint32_t rtos_free_heap_before_create;
    volatile uint32_t rtos_free_heap_after_create;
    volatile uint32_t heartbeat_loop_count;
    volatile uint32_t heartbeat_last_freertos_tick;
    volatile uint32_t heartbeat_last_now_ms;
    volatile uint32_t heartbeat_stack_high_water_mark;
    volatile uint32_t button_loop_count;
    volatile uint32_t button_last_freertos_tick;
    volatile uint32_t button_last_now_ms;
    volatile uint32_t button_stack_high_water_mark;
    volatile uint32_t led_loop_count;
    volatile uint32_t led_last_freertos_tick;
    volatile uint32_t led_last_now_ms;
    volatile uint32_t led_stack_high_water_mark;
    volatile uint32_t can_loop_count;
    volatile uint32_t can_last_freertos_tick;
    volatile uint32_t can_last_now_ms;
    volatile uint32_t can_stack_high_water_mark;
    AppCore          app;
} RuntimeContext;

static RuntimeContext g_runtime;

// task 문맥에서 app logic에 넘길 now_ms는 FreeRTOS tick 기준으로만 계산한다.
// CAN slave runtime은 더 이상 별도 RuntimeTick을 공용 task 시계로 사용하지 않는다.
static uint32_t Runtime_GetTaskContextNowMsFromTick(TickType_t now_tick)
{
    return (uint32_t)((((uint64_t)now_tick) * 1000ULL) / (uint64_t)configTICK_RATE_HZ);
}

// heartbeat를 dispatcher 밖의 native FreeRTOS task로 분리한 wrapper다.
// app 로직은 그대로 재사용하고,
// 주기 보장은 FreeRTOS의 vTaskDelayUntil()에 맡긴다.
static void Runtime_TaskHeartbeatNative(void *context)
{
    TickType_t dispatch_ticks;
    TickType_t last_wake_tick;
    TickType_t now_tick;
    uint32_t   now_ms;

    (void)context;

    dispatch_ticks = pdMS_TO_TICKS(APP_TASK_HEARTBEAT_MS);
    if (dispatch_ticks == 0U)
    {
        dispatch_ticks = 1U;
    }

    g_runtime.rtos_heartbeat_entered = 1U;
    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_tick = xTaskGetTickCount();
        now_ms = Runtime_GetTaskContextNowMsFromTick(now_tick);
        g_runtime.heartbeat_loop_count++;
        g_runtime.heartbeat_last_freertos_tick = (uint32_t)now_tick;
        g_runtime.heartbeat_last_now_ms = now_ms;
        g_runtime.heartbeat_stack_high_water_mark = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

        AppCore_TaskHeartbeat(&g_runtime.app, now_ms);

        vTaskDelayUntil(&last_wake_tick, dispatch_ticks);
    }
}

// button을 dispatcher 밖의 native FreeRTOS task로 분리한 wrapper다.
// debounce와 OK 요청 예약 정책은 기존 app 로직을 재사용하고,
// RTOS는 주기 실행만 맡는다.
static void Runtime_TaskButtonNative(void *context)
{
    TickType_t dispatch_ticks;
    TickType_t last_wake_tick;
    TickType_t now_tick;
    uint32_t   now_ms;

    (void)context;

    dispatch_ticks = pdMS_TO_TICKS(APP_TASK_BUTTON_MS);
    if (dispatch_ticks == 0U)
    {
        dispatch_ticks = 1U;
    }

    g_runtime.rtos_button_entered = 1U;
    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_tick = xTaskGetTickCount();
        now_ms = Runtime_GetTaskContextNowMsFromTick(now_tick);
        g_runtime.button_loop_count++;
        g_runtime.button_last_freertos_tick = (uint32_t)now_tick;
        g_runtime.button_last_now_ms = now_ms;
        g_runtime.button_stack_high_water_mark = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

        AppCore_TaskButton(&g_runtime.app, now_ms);

        vTaskDelayUntil(&last_wake_tick, dispatch_ticks);
    }
}

// LED를 dispatcher 밖의 native FreeRTOS task로 분리한 wrapper다.
// blink 진행과 mode 복귀는 기존 app/driver 로직을 재사용하고,
// RTOS는 실행 주기만 맡는다.
static void Runtime_TaskLedNative(void *context)
{
    TickType_t dispatch_ticks;
    TickType_t last_wake_tick;
    TickType_t now_tick;
    uint32_t   now_ms;

    (void)context;

    dispatch_ticks = pdMS_TO_TICKS(APP_TASK_LED_MS);
    if (dispatch_ticks == 0U)
    {
        dispatch_ticks = 1U;
    }

    g_runtime.rtos_led_entered = 1U;
    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_tick = xTaskGetTickCount();
        now_ms = Runtime_GetTaskContextNowMsFromTick(now_tick);
        g_runtime.led_loop_count++;
        g_runtime.led_last_freertos_tick = (uint32_t)now_tick;
        g_runtime.led_last_now_ms = now_ms;
        g_runtime.led_stack_high_water_mark = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

        AppCore_TaskLed(&g_runtime.app, now_ms);

        vTaskDelayUntil(&last_wake_tick, dispatch_ticks);
    }
}

// CAN 처리를 native FreeRTOS task로 실행하는 wrapper다.
// transport 진행, 결과 소비, 수신 command 처리는 기존 AppCore 경로를 그대로 재사용한다.
static void Runtime_TaskCanNative(void *context)
{
    TickType_t dispatch_ticks;
    TickType_t last_wake_tick;
    TickType_t now_tick;
    uint32_t   now_ms;

    (void)context;

    dispatch_ticks = pdMS_TO_TICKS(APP_TASK_CAN_MS);
    if (dispatch_ticks == 0U)
    {
        dispatch_ticks = 1U;
    }

    g_runtime.rtos_can_entered = 1U;
    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_tick = xTaskGetTickCount();
        now_ms = Runtime_GetTaskContextNowMsFromTick(now_tick);
        g_runtime.can_loop_count++;
        g_runtime.can_last_freertos_tick = (uint32_t)now_tick;
        g_runtime.can_last_now_ms = now_ms;
        g_runtime.can_stack_high_water_mark = (uint32_t)uxTaskGetStackHighWaterMark(NULL);

        AppCore_TaskCan(&g_runtime.app, now_ms);

        vTaskDelayUntil(&last_wake_tick, dispatch_ticks);
    }
}

// 초기화 실패 뒤 더 진행하지 않도록 멈춰 두는 loop다.
// 현재는 별도 진단 출력 없이 멈추는 단순한 형태라,
// bring-up 단계에서 최소 안전 정지점으로만 쓰고 있다.
// 주의: 실제 디버깅 단계에서는 마지막 오류 코드 보존이나 LED fault pattern이 있으면 훨씬 낫다.
static void Runtime_FaultLoop(void)
{
    for (;;)
    {
    }
}

// 보드와 app을 차례대로 준비하고 task 실행 전 상태를 완성한다.
// 시작 직후 순서를 이 함수에 모아 두어,
// main 쪽에서는 성공 여부만 보고 바로 runtime을 시작할 수 있게 한다.
InfraStatus Runtime_Init(void)
{
    AppCoreConfig app_config;
    InfraStatus status;

    g_runtime.initialized = 0U;
    g_runtime.init_status = INFRA_STATUS_NOT_READY;
    g_runtime.rtos_heartbeat_create_status = -1;
    g_runtime.rtos_button_create_status = -1;
    g_runtime.rtos_led_create_status = -1;
    g_runtime.rtos_can_create_status = -1;
    g_runtime.rtos_scheduler_start_called = 0U;
    g_runtime.rtos_scheduler_returned = 0U;
    g_runtime.rtos_heartbeat_entered = 0U;
    g_runtime.rtos_button_entered = 0U;
    g_runtime.rtos_led_entered = 0U;
    g_runtime.rtos_can_entered = 0U;
    g_runtime.rtos_free_heap_before_create = 0U;
    g_runtime.rtos_free_heap_after_create = 0U;
    g_runtime.heartbeat_loop_count = 0U;
    g_runtime.heartbeat_last_freertos_tick = 0U;
    g_runtime.heartbeat_last_now_ms = 0U;
    g_runtime.heartbeat_stack_high_water_mark = 0U;
    g_runtime.button_loop_count = 0U;
    g_runtime.button_last_freertos_tick = 0U;
    g_runtime.button_last_now_ms = 0U;
    g_runtime.button_stack_high_water_mark = 0U;
    g_runtime.led_loop_count = 0U;
    g_runtime.led_last_freertos_tick = 0U;
    g_runtime.led_last_now_ms = 0U;
    g_runtime.led_stack_high_water_mark = 0U;
    g_runtime.can_loop_count = 0U;
    g_runtime.can_last_freertos_tick = 0U;
    g_runtime.can_last_now_ms = 0U;
    g_runtime.can_stack_high_water_mark = 0U;

    // 1) board를 먼저 올린다.
    status = BoardHw_Init();
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    // 2) role별 app 설정을 채운 뒤 AppCore를 초기화한다.
    app_config.local_node_id = APP_NODE_ID_SLAVE1;
    app_config.can_default_timeout_ms = APP_CAN_DEFAULT_TIMEOUT_MS;
    app_config.can_max_submit_per_tick = APP_CAN_MAX_SUBMIT_PER_TICK;
    status = AppCore_Init(&g_runtime.app, &app_config);
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    // 3) app 준비가 끝나면 runtime은 native task 생성만 담당한다.
    // task 문맥 공용 시간 기준은 FreeRTOS tick으로 유지한다.
    g_runtime.initialized = 1U;
    g_runtime.init_status = INFRA_STATUS_OK;
    return INFRA_STATUS_OK;
}

// 준비가 끝난 뒤 native app task를 모두 만들고 scheduler를 시작한다.
// heartbeat/button/led/can이 각각 FreeRTOS task로 실행된다.
void Runtime_Run(void)
{
    BaseType_t heartbeat_create_status;
    BaseType_t button_create_status;
    BaseType_t led_create_status;
    BaseType_t can_create_status;

    if ((g_runtime.initialized == 0U) || (g_runtime.init_status != INFRA_STATUS_OK))
    {
        Runtime_FaultLoop();
    }

    g_runtime.rtos_free_heap_before_create = (uint32_t)xPortGetFreeHeapSize();
    heartbeat_create_status = xTaskCreate(Runtime_TaskHeartbeatNative,
                                          "app_hb",
                                          APP_RTOS_HEARTBEAT_TASK_STACK_WORDS,
                                          NULL,
                                          APP_RTOS_HEARTBEAT_TASK_PRIORITY,
                                          NULL);
    g_runtime.rtos_heartbeat_create_status = (int32_t)heartbeat_create_status;
    g_runtime.rtos_free_heap_after_create = (uint32_t)xPortGetFreeHeapSize();
    if (heartbeat_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    button_create_status = xTaskCreate(Runtime_TaskButtonNative,
                                       "app_btn",
                                       APP_RTOS_BUTTON_TASK_STACK_WORDS,
                                       NULL,
                                       APP_RTOS_BUTTON_TASK_PRIORITY,
                                       NULL);
    g_runtime.rtos_button_create_status = (int32_t)button_create_status;
    g_runtime.rtos_free_heap_after_create = (uint32_t)xPortGetFreeHeapSize();
    if (button_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    led_create_status = xTaskCreate(Runtime_TaskLedNative,
                                    "app_led",
                                    APP_RTOS_LED_TASK_STACK_WORDS,
                                    NULL,
                                    APP_RTOS_LED_TASK_PRIORITY,
                                    NULL);
    g_runtime.rtos_led_create_status = (int32_t)led_create_status;
    g_runtime.rtos_free_heap_after_create = (uint32_t)xPortGetFreeHeapSize();
    if (led_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    can_create_status = xTaskCreate(Runtime_TaskCanNative,
                                    "app_can",
                                    APP_RTOS_CAN_TASK_STACK_WORDS,
                                    NULL,
                                    APP_RTOS_CAN_TASK_PRIORITY,
                                    NULL);
    g_runtime.rtos_can_create_status = (int32_t)can_create_status;
    g_runtime.rtos_free_heap_after_create = (uint32_t)xPortGetFreeHeapSize();
    if (can_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    g_runtime.rtos_scheduler_start_called = 1U;
    vTaskStartScheduler();
    g_runtime.rtos_scheduler_returned = 1U;
    Runtime_FaultLoop();
}

// 참고:
// 현재 fault loop는 매우 단순하므로,
// 실제 디버깅 단계에서는 마지막 실패 원인이나 LED 표시를 함께 유지하는 편이 유리하다.
