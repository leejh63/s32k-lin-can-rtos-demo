// 가벼운 주기 task 스케줄러 정의를 담는 헤더다.
// runtime 계층은 이 테이블 기반 인터페이스를 사용해
// cooperative super-loop에서 AppCore task를 호출한다.
#ifndef RUNTIME_TASK_H
#define RUNTIME_TASK_H

#include "infra_types.h"

typedef void (*RuntimeTaskFn)(void *context, uint32_t now_ms);

// cooperative task 테이블의 한 항목이다.
// runtime은 주기, 마지막 실행 시각, callback,
// context를 함께 저장해 스케줄링을 데이터 중심으로 유지한다.
typedef struct
{
    const char   *name;
    uint32_t      period_ms;
    uint32_t      last_run_ms;
    RuntimeTaskFn task_fn;
    void         *context;
} RuntimeTaskEntry;

// 모든 task timestamp를 같은 시작 시점으로 맞춘다.
// 초기화가 끝난 뒤 첫 실행 시점을 runtime tick 기준에
// 맞춰 정렬하기 위한 함수다.
void RuntimeTask_ResetTable(RuntimeTaskEntry *table,
                            uint32_t task_count,
                            uint32_t start_ms);
void RuntimeTask_RunDue(RuntimeTaskEntry *table,
                        uint32_t task_count,
                        uint32_t now_ms);

#endif
