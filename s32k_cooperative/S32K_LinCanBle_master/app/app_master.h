// master 노드 역할의 policy 인터페이스다.
// master는 LIN 센서 상태와 CAN 현장 동작,
// 그리고 emergency 해제 승인 절차를 조정한다.
#ifndef APP_MASTER_H
#define APP_MASTER_H

#include "app_core.h"
#include "../services/can_types.h"
#include "../services/lin_module.h"

// master 역할 policy의 진입점 모음이다.
// AppCore는 이 함수들로 coordinator 로직을 초기화하고,
// 새 LIN 상태와 승인 요청에 대응한다.
InfraStatus AppMaster_Init(AppCore *app);
void        AppMaster_RequestOk(AppCore *app, uint32_t now_ms);
void        AppMaster_HandleFreshLinStatus(AppCore *app, const LinStatusFrame *status);
void        AppMaster_HandleCanCommand(AppCore *app,
                                       const CanMessage *message,
                                       uint32_t now_ms,
                                       uint8_t *out_response_code);
void        AppMaster_AfterLinPoll(AppCore *app, uint32_t now_ms);

#endif
