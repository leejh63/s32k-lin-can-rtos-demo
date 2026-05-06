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

uint8_t IsoSdk_LinIsSupported(void);
uint8_t IsoSdk_LinInit(IsoSdkLinContext *context,
                       uint8_t role,
                       uint16_t timeout_ticks,
                       IsoSdkLinEventCallback event_cb,
                       void *event_context);
uint8_t IsoSdk_LinMasterSendHeader(IsoSdkLinContext *context, uint8_t pid);
uint8_t IsoSdk_LinStartReceive(IsoSdkLinContext *context, uint8_t *buffer, uint8_t length);
uint8_t IsoSdk_LinStartSend(IsoSdkLinContext *context, const uint8_t *buffer, uint8_t length);
void    IsoSdk_LinGotoIdle(IsoSdkLinContext *context);
void    IsoSdk_LinSetTimeout(IsoSdkLinContext *context, uint16_t timeout_ticks);
void    IsoSdk_LinServiceTick(IsoSdkLinContext *context);

#endif
