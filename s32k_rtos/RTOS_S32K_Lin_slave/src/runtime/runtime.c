// LIN sensor slave 전용 runtime 구현 파일이다.
// 센서 노드가 실제로 쓰는 task만 등록하여,
// CAN, UART, render 같은 다른 노드용 스케줄 항목을 제거한다.
#include "runtime.h"

#include "FreeRTOS.h"
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
} RuntimeContext;

static RuntimeContext g_runtime;

// task 문맥에서 app logic에 넘길 now_ms는 FreeRTOS tick 기준으로만 계산한다.
// LIN timeout service 같은 ISR 경로는 RuntimeTick이 따로 맡는다.
static uint32_t Runtime_GetTaskContextNowMs(void)
{
    TickType_t now_ticks;

    now_ticks = xTaskGetTickCount();
    return (uint32_t)((((uint64_t)now_ticks) * 1000ULL) / (uint64_t)configTICK_RATE_HZ);
}

// 초기화 실패 시 빠져나오지 않는 fault loop다.
static void Runtime_FaultLoop(void)
{
    for (;;)
    {
    }
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
        AppCore_TaskHeartbeat(&g_runtime.app, now_ms);
        vTaskDelayUntil(&last_wake_tick, heartbeat_ticks);
    }
}

// LED만 dispatcher 밖의 native FreeRTOS task로 분리한 wrapper다.
// 기존 AppCore 로직은 그대로 재사용하고,
// 주기 보장은 FreeRTOS의 vTaskDelayUntil()에 맡긴다.
static void Runtime_LedTask(void *argument)
{
    TickType_t led_ticks;
    TickType_t last_wake_tick;
    uint32_t   now_ms;

    (void)argument;

    led_ticks = pdMS_TO_TICKS(APP_RTOS_LED_PERIOD_MS);
    if (led_ticks == 0U)
    {
        led_ticks = 1U;
    }

    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_ms = Runtime_GetTaskContextNowMs();
        AppCore_TaskLed(&g_runtime.app, now_ms);
        vTaskDelayUntil(&last_wake_tick, led_ticks);
    }
}

// ADC만 dispatcher 밖의 native FreeRTOS task로 분리한 wrapper다.
// 기존 AppCore 로직은 그대로 재사용하고,
// 주기 보장은 FreeRTOS의 vTaskDelayUntil()에 맡긴다.
static void Runtime_AdcTask(void *argument)
{
    TickType_t adc_ticks;
    TickType_t last_wake_tick;
    uint32_t   now_ms;

    (void)argument;

    adc_ticks = pdMS_TO_TICKS(APP_RTOS_ADC_PERIOD_MS);
    if (adc_ticks == 0U)
    {
        adc_ticks = 1U;
    }

    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_ms = Runtime_GetTaskContextNowMs();
        AppCore_TaskAdc(&g_runtime.app, now_ms);
        vTaskDelayUntil(&last_wake_tick, adc_ticks);
    }
}

// lin_fast를 마지막으로 dispatcher 밖의 native FreeRTOS task로 분리한 wrapper다.
// 기존 AppCore fast path 로직은 그대로 재사용하고,
// task wake-up 주기만 FreeRTOS의 vTaskDelayUntil()에 맡긴다.
static void Runtime_LinFastTask(void *argument)
{
    TickType_t lin_fast_ticks;
    TickType_t last_wake_tick;
    uint32_t   now_ms;

    (void)argument;

    lin_fast_ticks = pdMS_TO_TICKS(APP_RTOS_LIN_FAST_PERIOD_MS);
    if (lin_fast_ticks == 0U)
    {
        lin_fast_ticks = 1U;
    }

    last_wake_tick = xTaskGetTickCount();

    for (;;)
    {
        now_ms = Runtime_GetTaskContextNowMs();
        AppCore_TaskLinFast(&g_runtime.app, now_ms);
        vTaskDelayUntil(&last_wake_tick, lin_fast_ticks);
    }
}

// runtime, 보드, tick, app, ISR hook를 차례대로 준비한다.
// 어느 한 단계라도 실패하면 이후 run 단계로 가지 않게 막는다.
// 주의: hook 등록을 AppCore 초기화 뒤에 두어, 미초기화 LIN 모듈에 ISR이 먼저 들어가는 일을 피한다.
InfraStatus Runtime_Init(void)
{
    InfraStatus status;

    g_runtime.initialized = 0U;
    g_runtime.init_status = INFRA_STATUS_NOT_READY;

    // 1) 보드와 외부 트랜시버를 먼저 올린다.
    status = RuntimeIo_BoardInit();
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    // 2) ISR 기반 LIN timeout service 경로를 준비한다.
    status = RuntimeTick_Init();
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    // 3) application 조립을 끝낸 뒤에만 ISR hook를 등록한다.
    status = AppCore_Init(&g_runtime.app);
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    RuntimeTick_ClearHooks();
    // 4) LIN timeout service로 이어지는 짧은 ISR hook를 연결한다.
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

// Lin_slave의 모든 주기 task를 native FreeRTOS task로 만든 뒤 scheduler를 시작한다.
void Runtime_Run(void)
{
    BaseType_t lin_fast_create_status;
    BaseType_t heartbeat_create_status;
    BaseType_t led_create_status;
    BaseType_t adc_create_status;

    if ((g_runtime.initialized == 0U) || (g_runtime.init_status != INFRA_STATUS_OK))
    {
        Runtime_FaultLoop();
    }

    lin_fast_create_status = xTaskCreate(Runtime_LinFastTask,
                                         "app_lin_fast",
                                         APP_RTOS_LIN_FAST_TASK_STACK_WORDS,
                                         NULL,
                                         APP_RTOS_LIN_FAST_TASK_PRIORITY,
                                         NULL);
    if (lin_fast_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    heartbeat_create_status = xTaskCreate(Runtime_HeartbeatTask,
                                          "app_hb",
                                          APP_RTOS_HEARTBEAT_TASK_STACK_WORDS,
                                          NULL,
                                          APP_RTOS_HEARTBEAT_TASK_PRIORITY,
                                          NULL);
    if (heartbeat_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    led_create_status = xTaskCreate(Runtime_LedTask,
                                    "app_led",
                                    APP_RTOS_LED_TASK_STACK_WORDS,
                                    NULL,
                                    APP_RTOS_LED_TASK_PRIORITY,
                                    NULL);
    if (led_create_status != pdPASS)
    {
        Runtime_FaultLoop();
    }

    adc_create_status = xTaskCreate(Runtime_AdcTask,
                                    "app_adc",
                                    APP_RTOS_ADC_TASK_STACK_WORDS,
                                    NULL,
                                    APP_RTOS_ADC_TASK_PRIORITY,
                                    NULL);
    if (adc_create_status != pdPASS)
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

// 참고:
// fault loop가 아주 단순해서 bring-up 중 실패 원인을 현장에서 바로 알기는 어렵다.
// 확장 시에는 마지막 init 실패 코드를 LED나 간단한 상태 변수로 남기는 정도만 더해도 도움이 된다.
