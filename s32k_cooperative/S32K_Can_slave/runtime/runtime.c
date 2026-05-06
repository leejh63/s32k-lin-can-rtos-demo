// CAN 현장 반응 slave 최소 운영 구성용 runtime 구현 파일이다.
// slave1이 실제로 쓰는 button, can, led task만 등록하여,
// 보드 초기화와 scheduler 구성을 runtime에만 남긴다.
#include "runtime.h"

#include "../app/app_core_internal.h"
#include "../app/app_config.h"
#include "../core/runtime_task.h"
#include "../core/runtime_tick.h"
#include "../drivers/board_hw.h"

typedef struct
{
    uint8_t          initialized;
    InfraStatus      init_status;
    AppCore          app;
    RuntimeTaskEntry tasks[4];
} RuntimeContext;

static RuntimeContext g_runtime;

// heartbeat task wrapper다.
// scheduler 테이블에서는 공통 시그니처를 쓰므로,
// 실제 app task로 들어가기 전에 얇은 연결 함수를 한 번 둔다.
static void Runtime_TaskHeartbeat(void *context, uint32_t now_ms)
{
    AppCore_TaskHeartbeat((AppCore *)context, now_ms);
}

// CAN task wrapper다.
// runtime은 task 표만 관리하고,
// 실제 통신 처리 흐름은 AppCore에 위임한다.
static void Runtime_TaskCan(void *context, uint32_t now_ms)
{
    AppCore_TaskCan((AppCore *)context, now_ms);
}

// 버튼 task wrapper다.
// 보드 입력 판독과 debounce 정책은 app 쪽에 남기고,
// runtime은 호출 시점만 맞춰 준다.
static void Runtime_TaskButton(void *context, uint32_t now_ms)
{
    AppCore_TaskButton((AppCore *)context, now_ms);
}

// LED task wrapper다.
// blink 진행과 모드 반영은 app/driver 쪽에서 맡고,
// runtime은 주기적으로 실행 기회만 제공한다.
static void Runtime_TaskLed(void *context, uint32_t now_ms)
{
    AppCore_TaskLed((AppCore *)context, now_ms);
}

// slave1에 필요한 task 목록을 runtime 테이블에 채운다.
// task 이름과 주기, 진입 함수를 한곳에 모아 두면
// super-loop 구성을 읽을 때 전체 흐름이 파악하기 쉬워진다.
static void Runtime_BuildTaskTable(RuntimeContext *runtime)
{
    runtime->tasks[0].name        = "button";
    runtime->tasks[0].period_ms   = APP_TASK_BUTTON_MS;
    runtime->tasks[0].last_run_ms = 0U;
    runtime->tasks[0].task_fn     = Runtime_TaskButton;
    runtime->tasks[0].context     = &runtime->app;

    runtime->tasks[1].name        = "can";
    runtime->tasks[1].period_ms   = APP_TASK_CAN_MS;
    runtime->tasks[1].last_run_ms = 0U;
    runtime->tasks[1].task_fn     = Runtime_TaskCan;
    runtime->tasks[1].context     = &runtime->app;

    runtime->tasks[2].name        = "led";
    runtime->tasks[2].period_ms   = APP_TASK_LED_MS;
    runtime->tasks[2].last_run_ms = 0U;
    runtime->tasks[2].task_fn     = Runtime_TaskLed;
    runtime->tasks[2].context     = &runtime->app;

    runtime->tasks[3].name        = "heartbeat";
    runtime->tasks[3].period_ms   = APP_TASK_HEARTBEAT_MS;
    runtime->tasks[3].last_run_ms = 0U;
    runtime->tasks[3].task_fn     = Runtime_TaskHeartbeat;
    runtime->tasks[3].context     = &runtime->app;
}

// 초기화 실패 뒤 더 진행하지 않도록 멈춰 두는 loop다.
// 현재는 별도 진단 출력 없이 멈추는 단순한 형태라,
// bring-up 단계에서 최소 안전 정지점으로만 쓰고 있다.
static void Runtime_FaultLoop(void)
{
    for (;;)
    {
    }
}

// 보드, tick, app을 차례대로 준비하고 task 테이블을 완성한다.
// 시작 직후 순서를 이 함수에 모아 두어,
// main 쪽에서는 성공 여부만 보고 바로 runtime을 시작할 수 있게 한다.
InfraStatus Runtime_Init(void)
{
    AppCoreConfig app_config;
    InfraStatus status;
    uint32_t    start_ms;

    g_runtime.initialized = 0U;
    g_runtime.init_status = INFRA_STATUS_NOT_READY;

    status = BoardHw_Init();
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

    app_config.local_node_id = APP_NODE_ID_SLAVE1;
    app_config.can_default_timeout_ms = APP_CAN_DEFAULT_TIMEOUT_MS;
    app_config.can_max_submit_per_tick = APP_CAN_MAX_SUBMIT_PER_TICK;
    status = AppCore_Init(&g_runtime.app, &app_config);
    if (status != INFRA_STATUS_OK)
    {
        g_runtime.init_status = status;
        return status;
    }

    Runtime_BuildTaskTable(&g_runtime);
    start_ms = RuntimeTick_GetMs();
    RuntimeTask_ResetTable(g_runtime.tasks,
                           INFRA_ARRAY_COUNT(g_runtime.tasks),
                           start_ms);

    g_runtime.initialized = 1U;
    g_runtime.init_status = INFRA_STATUS_OK;
    return INFRA_STATUS_OK;
}

// 준비가 끝난 뒤 cooperative super-loop를 계속 돌린다.
// 매 반복마다 현재 시간을 읽어 due task만 실행하므로,
// 각 기능은 자기 주기 안에서 비교적 독립적으로 움직일 수 있다.
void Runtime_Run(void)
{
    if ((g_runtime.initialized == 0U) || (g_runtime.init_status != INFRA_STATUS_OK))
    {
        Runtime_FaultLoop();
    }

    for (;;)
    {
        RuntimeTask_RunDue(g_runtime.tasks,
                           INFRA_ARRAY_COUNT(g_runtime.tasks),
            RuntimeTick_GetMs());
    }
}

// 참고:
// 현재 fault loop는 매우 단순하므로,
// 실제 디버깅 단계에서는 마지막 실패 원인이나 LED 표시를 함께 유지하는 편이 유리하다.
