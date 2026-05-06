// CAN 기반 현장 반응 노드인 slave1의 policy 인터페이스다.
// slave1은 LED 출력과 버튼 승인 입력,
// master에서 오는 CAN 명령 처리에 집중한다.
#ifndef APP_SLAVE1_H
#define APP_SLAVE1_H

#include "app_core_internal.h"

// slave1 policy의 진입점 모음이다.
// app core는 이 hook들로 현장 반응 노드를 올리고,
// 버튼과 LED 상태기계를 처리한다.
InfraStatus AppSlave1_Init(AppCore *app);
void        AppSlave1_HandleCanCommand(AppCore *app,
                                       const CanMessage *message,
                                       uint8_t *out_response_code);
void        AppSlave1_TaskButton(AppCore *app, uint32_t now_ms);
void        AppSlave1_TaskLed(AppCore *app, uint32_t now_ms);

#endif
