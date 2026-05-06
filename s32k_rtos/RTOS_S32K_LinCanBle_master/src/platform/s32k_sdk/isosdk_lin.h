#ifndef ISOSDK_LIN_H
#define ISOSDK_LIN_H

#include <stdint.h>

typedef enum
{
    ISOSDK_LIN_ROLE_MASTER = 1,
    ISOSDK_LIN_ROLE_SLAVE = 2
} IsoSdkLinRole;

typedef enum
{
    ISOSDK_LIN_EVENT_NONE = 0,
    ISOSDK_LIN_EVENT_PID_OK,
    ISOSDK_LIN_EVENT_RX_DONE,
    ISOSDK_LIN_EVENT_TX_DONE,
    ISOSDK_LIN_EVENT_ERROR
} IsoSdkLinEventId;

typedef void (*IsoSdkLinEventCallback)(void *context, uint8_t event_id, uint8_t current_pid);

typedef struct
{
    uint8_t                initialized;
    uint16_t               timeout_ticks;
    uint8_t                role;
    IsoSdkLinEventCallback event_cb;
    void                  *event_context;
} IsoSdkLinContext;

// IsoSdk_Lin 지원 여부를 확인한다.
uint8_t IsoSdk_LinIsSupported(void);
uint8_t IsoSdk_LinInit(IsoSdkLinContext *context,
                       uint8_t role,
                       uint16_t timeout_ticks,
                       IsoSdkLinEventCallback event_cb,
                       void *event_context);
// IsoSdk_Lin로 LIN header를 송신한다.
uint8_t IsoSdk_LinMasterSendHeader(IsoSdkLinContext *context, uint8_t pid);
// IsoSdk_Lin 수신을 시작한다.
uint8_t IsoSdk_LinStartReceive(IsoSdkLinContext *context, uint8_t *buffer, uint8_t length);
// IsoSdk_Lin 송신을 시작한다.
uint8_t IsoSdk_LinStartSend(IsoSdkLinContext *context, const uint8_t *buffer, uint8_t length);
// IsoSdk_Lin 상태를 idle로 돌린다.
void    IsoSdk_LinGotoIdle(IsoSdkLinContext *context);
// IsoSdk_Lin timeout 기준을 설정한다.
void    IsoSdk_LinSetTimeout(IsoSdkLinContext *context, uint16_t timeout_ticks);
// IsoSdk_Lin tick 기반 유지 작업을 수행한다.
void    IsoSdk_LinServiceTick(IsoSdkLinContext *context);

#endif
