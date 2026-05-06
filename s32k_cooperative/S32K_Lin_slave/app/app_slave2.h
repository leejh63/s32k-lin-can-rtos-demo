// LIN 센서 노드인 slave2의 policy 인터페이스다.
// slave2는 ADC 샘플링과 LIN 상태 제공,
// 그리고 승인 절차로만 해제되는 emergency latch를 담당한다.
#ifndef APP_SLAVE2_H
#define APP_SLAVE2_H

#include "app_core.h"

// slave2 policy의 진입점 모음이다.
// AppCore는 이 함수들로 센서 쪽 로직을 초기화하고,
// ADC 샘플링과 master 승인 token 처리를 수행한다.
InfraStatus AppSlave2_Init(AppCore *app);
void        AppSlave2_HandleLinOkToken(AppCore *app);
void        AppSlave2_TaskAdc(AppCore *app, uint32_t now_ms);

#endif
