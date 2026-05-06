#ifndef LIN_HW_H
#define LIN_HW_H

#include "../core/infra_types.h"

typedef enum
{
    LIN_HW_ROLE_MASTER = 1,
    LIN_HW_ROLE_SLAVE = 2
} LinHwRole;

typedef enum
{
    LIN_HW_EVENT_NONE = 0,
    LIN_HW_EVENT_PID_OK,
    LIN_HW_EVENT_RX_DONE,
    LIN_HW_EVENT_TX_DONE,
    LIN_HW_EVENT_ERROR
} LinHwEventId;

typedef void (*LinHwEventBridgeFn)(void *context, LinHwEventId event_id, uint8_t current_pid);

// LinHw 지원 여부를 확인한다.
uint8_t     LinHw_IsSupported(void);
// LinHw 동작 설정을 저장한다.
void        LinHw_Configure(LinHwRole role, uint16_t timeout_ticks);
// LinHw 연결 대상을 붙인다.
void        LinHw_AttachEventBridge(void *context, LinHwEventBridgeFn event_bridge);
// LinHw를 초기화한다.
InfraStatus LinHw_Init(void *context);
// LinHw로 LIN header를 송신한다.
InfraStatus LinHw_MasterSendHeader(void *context, uint8_t pid);
// LinHw 수신을 시작한다.
InfraStatus LinHw_StartReceive(void *context, uint8_t *buffer, uint8_t length);
// LinHw 송신을 시작한다.
InfraStatus LinHw_StartSend(void *context, const uint8_t *buffer, uint8_t length);
// LinHw 상태를 idle로 돌린다.
void        LinHw_GotoIdle(void *context);
// LinHw timeout 기준을 설정한다.
void        LinHw_SetTimeout(void *context, uint16_t timeout_ticks);
// LinHw tick 기반 유지 작업을 수행한다.
void        LinHw_ServiceTick(void *context);

#endif
