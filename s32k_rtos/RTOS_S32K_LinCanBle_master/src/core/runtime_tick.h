// ISR 기반 runtime tick 소스의 공개 인터페이스다.
// task 문맥 공용 시계가 아니라,
// LIN timeout service 같은 짧은 ISR 경로를 연결하는 용도로 유지한다.
#ifndef RUNTIME_TICK_H
#define RUNTIME_TICK_H

#include "infra_types.h"

#ifndef RUNTIME_TICK_ISR_PERIOD_US
#define RUNTIME_TICK_ISR_PERIOD_US  500U
#endif

// 가벼운 tick 구독자가 사용하는 ISR hook 시그니처다.
// LIN 같은 모듈은 timeout 관리를
// 하드웨어 tick 가까이에서 하기 위해 여기 등록한다.
typedef void (*RuntimeTickHook)(void *context);

// ISR service용 runtime tick 소스를 시작하고 hook 등록 경로를 준비한다.
InfraStatus RuntimeTick_Init(void);
// ISR 기반 service 경로가 공유하는 millisecond 누적값을 조회한다.
// task 문맥 공용 시간 기준으로 쓰지 않는 것이 이번 단계 원칙이다.
uint32_t    RuntimeTick_GetMs(void);
// 원래 하드웨어 base tick이 몇 번 들어왔는지 조회한다.
uint32_t    RuntimeTick_GetBaseCount(void);
// RuntimeTick ISR hook를 모두 비운다.
void        RuntimeTick_ClearHooks(void);
// RuntimeTick ISR hook를 등록한다.
InfraStatus RuntimeTick_RegisterHook(RuntimeTickHook hook, void *context);

#endif
