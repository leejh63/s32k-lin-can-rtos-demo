// ISR/service용 runtime tick 소스의 공개 인터페이스다.
// CAN slave task 문맥의 공용 시계는 FreeRTOS tick이 맡고,
// 이 계층은 필요할 때만 별도 ISR/service 경로를 잇는 용도로 유지한다.
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

// 선택적인 ISR/service용 runtime tick 소스를 시작한다.
InfraStatus RuntimeTick_Init(void);
// ISR/service 경로가 공유할 때만 millisecond 누적값을 조회한다.
// task 문맥 공용 시간 기준으로 쓰지 않는 것이 현재 단계 원칙이다.
uint32_t    RuntimeTick_GetMs(void);
// 원래 하드웨어 base tick 횟수를 조회한다.
uint32_t    RuntimeTick_GetBaseCount(void);
// RuntimeTick ISR hook를 모두 비운다.
void        RuntimeTick_ClearHooks(void);
// RuntimeTick ISR hook를 등록한다.
InfraStatus RuntimeTick_RegisterHook(RuntimeTickHook hook, void *context);

#endif
