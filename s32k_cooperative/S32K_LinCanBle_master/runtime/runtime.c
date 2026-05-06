// master 노드 전용 runtime 구현 파일이다.
// coordinator가 실제로 쓰는 task만 등록하여,
// 다른 노드용 no-op task 없이 super-loop를 구성한다.
#include "runtime.h"

#include "../app/app_config.h"
#include "../app/app_core_internal.h"
#include "../core/runtime_task.h"
#include "../core/runtime_tick.h"
#include "runtime_io.h"

typedef struct
{
    uint8_t          initialized;
    InfraStatus      init_status;
    AppCore          app;
    RuntimeTaskEntry tasks[6];
} RuntimeContext;

static RuntimeContext g_runtime;

// AppCore heartbeat task를 runtime task 서명으로 감싼다.
static void Runtime_TaskHeartbeat(void *context, uint32_t now_ms)
{
    AppCore_TaskHeartbeat((AppCore *)context, now_ms);
}

// AppCore UART task를 runtime scheduler에 연결한다.
static void Runtime_TaskUart(void *context, uint32_t now_ms)
{
    AppCore_TaskUart((AppCore *)context, now_ms);
}

// AppCore CAN task를 runtime scheduler에 연결한다.
static void Runtime_TaskCan(void *context, uint32_t now_ms)
{
    AppCore_TaskCan((AppCore *)context, now_ms);
}

// AppCore LIN fast task를 runtime scheduler에 연결한다.
static void Runtime_TaskLinFast(void *context, uint32_t now_ms)
{
    AppCore_TaskLinFast((AppCore *)context, now_ms);
}

// AppCore LIN poll task를 runtime scheduler에 연결한다.
static void Runtime_TaskLinPoll(void *context, uint32_t now_ms)
{
    AppCore_TaskLinPoll((AppCore *)context, now_ms);
}

// AppCore render task를 runtime scheduler에 연결한다.
static void Runtime_TaskRender(void *context, uint32_t now_ms)
{
    AppCore_TaskRender((AppCore *)context, now_ms);
}

// master 역할에서 실제로 돌릴 cooperative task table을 구성한다.
// 주기와 순서는 이 함수 하나만 보면 정리되게 두었다.
static void Runtime_BuildTaskTable(RuntimeContext *runtime)
{
    runtime->tasks[0].name        = "uart";
    runtime->tasks[0].period_ms   = APP_TASK_UART_MS;
    runtime->tasks[0].last_run_ms = 0U;
    runtime->tasks[0].task_fn     = Runtime_TaskUart;
    runtime->tasks[0].context     = &runtime->app;

    runtime->tasks[1].name        = "lin_fast";
    runtime->tasks[1].period_ms   = APP_TASK_LIN_FAST_MS;
    runtime->tasks[1].last_run_ms = 0U;
    runtime->tasks[1].task_fn     = Runtime_TaskLinFast;
    runtime->tasks[1].context     = &runtime->app;

    runtime->tasks[2].name        = "can";
    runtime->tasks[2].period_ms   = APP_TASK_CAN_MS;
    runtime->tasks[2].last_run_ms = 0U;
    runtime->tasks[2].task_fn     = Runtime_TaskCan;
    runtime->tasks[2].context     = &runtime->app;

    runtime->tasks[3].name        = "lin_poll";
    runtime->tasks[3].period_ms   = APP_TASK_LIN_POLL_MS;
    runtime->tasks[3].last_run_ms = 0U;
    runtime->tasks[3].task_fn     = Runtime_TaskLinPoll;
    runtime->tasks[3].context     = &runtime->app;

    runtime->tasks[4].name        = "render";
    runtime->tasks[4].period_ms   = APP_TASK_RENDER_MS;
    runtime->tasks[4].last_run_ms = 0U;
    runtime->tasks[4].task_fn     = Runtime_TaskRender;
    runtime->tasks[4].context     = &runtime->app;

    runtime->tasks[5].name        = "heartbeat";
    runtime->tasks[5].period_ms   = APP_TASK_HEARTBEAT_MS;
    runtime->tasks[5].last_run_ms = 0U;
    runtime->tasks[5].task_fn     = Runtime_TaskHeartbeat;
    runtime->tasks[5].context     = &runtime->app;
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
    uint32_t    start_ms;

    g_runtime.initialized = 0U;
    g_runtime.init_status = INFRA_STATUS_NOT_READY;

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

    Runtime_BuildTaskTable(&g_runtime);
    start_ms = RuntimeTick_GetMs();
    RuntimeTask_ResetTable(g_runtime.tasks,
                           INFRA_ARRAY_COUNT(g_runtime.tasks),
                           start_ms);

    g_runtime.initialized = 1U;
    g_runtime.init_status = INFRA_STATUS_OK;
    return INFRA_STATUS_OK;
}

// master용 super-loop를 계속 돌린다.
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

// 현재 runtime이 들고 있는 AppCore를 조회한다.
const AppCore *Runtime_GetApp(void)
{
    return &g_runtime.app;
}

// 참고:
// fault loop가 아주 단순해서 bring-up 중 실패 원인을 현장에서 바로 알기는 어렵다.
// 확장 시에는 마지막 init 실패 코드를 LED나 콘솔로 남기는 정도만 더해도 도움이 된다.
