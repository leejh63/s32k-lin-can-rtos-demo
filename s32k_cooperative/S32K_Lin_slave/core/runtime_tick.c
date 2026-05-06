// LPTMR 기반 시스템 tick 구현 파일이다.
// 고정 주기의 base interrupt를 millisecond 시간으로 바꾸고,
// 등록된 소수의 hook에 같은 interrupt를 전달한다.
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

// system tick 카운터와 ISR hook table을 초기 상태로 맞춘다.
// 이후 TickHw가 인터럽트를 올리기 시작하면,
// runtime 전체가 같은 시간 기준을 쓰게 된다.
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

// 누적된 millisecond 시간을 읽어 온다.
uint32_t RuntimeTick_GetMs(void)
{
    return g_runtime_tick_ms;
}

// 원래 하드웨어 base tick이 몇 번 들어왔는지 반환한다.
uint32_t RuntimeTick_GetBaseCount(void)
{
    return g_runtime_tick_base_count;
}

// 등록된 ISR hook를 모두 비운다.
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
// 여기에는 flag 기록이나 짧은 timeout service 정도만 두는 편이 안전하다.
