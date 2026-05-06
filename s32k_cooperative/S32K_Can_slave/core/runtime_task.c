// runtime super-loop를 위한 cooperative 스케줄러 구현 파일이다.
// 이 함수들은 task 실행 시점만 판단하며,
// 애플리케이션 정책이나 하드웨어 상태는 직접 다루지 않는다.
#include "runtime_task.h"

#include <stddef.h>

// 모든 task 항목의 마지막 실행 시각을 같은 값으로 초기화한다.
// 스케줄러는 정의되지 않은 개별 값 대신,
// 하나의 공통 시작 직후 timestamp부터 주기를 계산하게 된다.
void RuntimeTask_ResetTable(RuntimeTaskEntry *table,
                            uint32_t task_count,
                            uint32_t start_ms)
{
    uint32_t index;

    if (table == NULL)
    {
        return;
    }

    for (index = 0U; index < task_count; index++)
    {
        table[index].last_run_ms = start_ms;
    }
}

// task 테이블을 순회하며 주기가 지난 항목을 실행한다.
// runtime 계층 super-loop가 사용하는
// cooperative scheduler의 핵심 함수다.
void RuntimeTask_RunDue(RuntimeTaskEntry *table,
                        uint32_t task_count,
                        uint32_t now_ms)
{
    uint32_t index;

    if (table == NULL)
    {
        return;
    }

    for (index = 0U; index < task_count; index++)
    {
        RuntimeTaskEntry *task = &table[index];

        if ((task->task_fn == NULL) || (task->period_ms == 0U))
        {
            continue;
        }

        if (Infra_TimeIsDue(now_ms, task->last_run_ms, task->period_ms) == 0U)
        {
            continue;
        }

        task->last_run_ms = now_ms;
        task->task_fn(task->context, now_ms);
    }
}
