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

uint8_t     LinHw_IsSupported(void);
void        LinHw_Configure(LinHwRole role, uint16_t timeout_ticks);
void        LinHw_AttachEventBridge(void *context, LinHwEventBridgeFn event_bridge);
InfraStatus LinHw_Init(void *context);
InfraStatus LinHw_MasterSendHeader(void *context, uint8_t pid);
InfraStatus LinHw_StartReceive(void *context, uint8_t *buffer, uint8_t length);
InfraStatus LinHw_StartSend(void *context, const uint8_t *buffer, uint8_t length);
void        LinHw_GotoIdle(void *context);
void        LinHw_SetTimeout(void *context, uint16_t timeout_ticks);
void        LinHw_ServiceTick(void *context);

#endif
