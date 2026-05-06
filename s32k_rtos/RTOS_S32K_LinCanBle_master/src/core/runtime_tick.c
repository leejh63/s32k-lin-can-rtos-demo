// LPTMR 기반 ISR service tick 구현 파일이다.
// task 문맥 공용 시간 기준이 아니라,
// LIN timeout service 같은 짧은 ISR 경로를 위한 base interrupt를 관리한다.
#include "runtime_tick.h"

#include <stddef.h>

#include "../drivers/tick_hw.h"

#define RUNTIME_TICK_HOOK_COUNT  4U

typedef struct
{
    RuntimeTickHook hook;
    void           *context;
} RuntimeTickHookSlot;

static volatile uint32_t   g_runtime_tick_ms = 0U;
static volatile uint32_t   g_runtime_tick_base_count = 0U;
static volatile uint32_t   g_runtime_tick_us_accumulator = 0U;
static RuntimeTickHookSlot g_runtime_tick_hooks[RUNTIME_TICK_HOOK_COUNT];

static void RuntimeTick_IrqHandler(void);

// ISR service용 tick 카운터와 hook table을 초기 상태로 맞춘다.
// task 문맥 시간 기준은 FreeRTOS tick이 따로 맡는다.
InfraStatus RuntimeTick_Init(void)
{
    uint32_t index;

    g_runtime_tick_ms = 0U;
    g_runtime_tick_base_count = 0U;
    g_runtime_tick_us_accumulator = 0U;

    for (index = 0U; index < RUNTIME_TICK_HOOK_COUNT; index++)
    {
        g_runtime_tick_hooks[index].hook = NULL;
        g_runtime_tick_hooks[index].context = NULL;
    }

    return TickHw_Init(RuntimeTick_IrqHandler);
}

// ISR service 경로가 공유하는 millisecond 누적값을 읽어 온다.
// task 문맥 공용 시계로 쓰지 않도록 역할을 축소한다.
uint32_t RuntimeTick_GetMs(void)
{
    return g_runtime_tick_ms;
}

// 원래 하드웨어 base tick이 몇 번 들어왔는지 반환한다.
// ISR 기반 service 경로 관찰용 참고값이다.
uint32_t RuntimeTick_GetBaseCount(void)
{
    return g_runtime_tick_base_count;
}

// 등록된 ISR hook를 모두 비운다.
// runtime 재초기화나 역할 전환처럼
// 예전 구독자를 한 번 정리하고 싶을 때 쓰는 재설정 함수다.
void RuntimeTick_ClearHooks(void)
{
    uint32_t index;

    for (index = 0U; index < RUNTIME_TICK_HOOK_COUNT; index++)
    {
        g_runtime_tick_hooks[index].hook = NULL;
        g_runtime_tick_hooks[index].context = NULL;
    }
}

// tick ISR 때 함께 불릴 가벼운 hook 하나를 등록한다.
// hook는 interrupt 문맥에서 바로 실행되므로,
// 오래 걸리는 작업보다 짧은 신호 전달에만 사용하는 것이 적절하다.
// hook table을 건드리는 경로라 동시성 위험이 있으면 짧은 크리티컬 섹션이 필요하다.
InfraStatus RuntimeTick_RegisterHook(RuntimeTickHook hook, void *context)
{
    uint32_t index;

    if (hook == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    for (index = 0U; index < RUNTIME_TICK_HOOK_COUNT; index++)
    {
        if (g_runtime_tick_hooks[index].hook == NULL)
        {
            g_runtime_tick_hooks[index].context = context;
            g_runtime_tick_hooks[index].hook = hook;
            return INFRA_STATUS_OK;
        }
    }

    return INFRA_STATUS_FULL;
}

// 하드웨어 tick을 software 시간과 hook 호출로 연결한다.
// 시간 누적과 hook 호출을 먼저 처리한 뒤,
// 마지막에 compare flag를 정리한다.
static void RuntimeTick_IrqHandler(void)
{
    uint32_t index;

    g_runtime_tick_base_count++;
    g_runtime_tick_us_accumulator += RUNTIME_TICK_ISR_PERIOD_US;

    while (g_runtime_tick_us_accumulator >= 1000U)
    {
        g_runtime_tick_ms++;
        g_runtime_tick_us_accumulator -= 1000U;
    }

    for (index = 0U; index < RUNTIME_TICK_HOOK_COUNT; index++)
    {
        if (g_runtime_tick_hooks[index].hook != NULL)
        {
            g_runtime_tick_hooks[index].hook(g_runtime_tick_hooks[index].context);
        }
    }

    TickHw_ClearCompareFlag();
}

// 참고:
// 등록된 hook는 모두 ISR 문맥에서 바로 실행되니,
// 여기에는 flag 기록이나 짧은 timeout service 정도만 두는 쪽이 유지보수에 유리하다.
