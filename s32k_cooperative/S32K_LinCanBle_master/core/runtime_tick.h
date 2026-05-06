// 시스템 tick 소스의 공개 인터페이스다.
// runtime과 통신 모듈은 이 계층을 통해
// millisecond 시간과 간단한 ISR hook 등록 기능을 사용한다.
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

// 시스템 tick 소스를 시작하고 millisecond 시간을 제공한다.
// runtime은 task 스케줄링이나 통신 timeout 처리 전에
// 이 기능을 한 번만 초기화한다.
InfraStatus RuntimeTick_Init(void);
uint32_t    RuntimeTick_GetMs(void);
uint32_t    RuntimeTick_GetBaseCount(void);
void        RuntimeTick_ClearHooks(void);
InfraStatus RuntimeTick_RegisterHook(RuntimeTickHook hook, void *context);

#endif
