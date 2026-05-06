#ifndef LIN_MODULE_INTERNAL_H
#define LIN_MODULE_INTERNAL_H

#include "lin_module.h"

typedef enum
{
    LIN_MODULE_STATE_IDLE = 0,
    LIN_MODULE_STATE_WAIT_PID,
    LIN_MODULE_STATE_WAIT_RX,
    LIN_MODULE_STATE_WAIT_TX
} LinModuleState;

typedef enum
{
    LIN_MODULE_EVENT_FLAG_NONE = 0U,
    LIN_MODULE_EVENT_FLAG_PID_OK = (1UL << 0),
    LIN_MODULE_EVENT_FLAG_RX_DONE = (1UL << 1),
    LIN_MODULE_EVENT_FLAG_TX_DONE = (1UL << 2),
    LIN_MODULE_EVENT_FLAG_ERROR = (1UL << 3)
} LinModuleEventFlag;

struct LinModule
{
    uint8_t           initialized;
    LinConfig         config;
    volatile LinModuleState state;
    volatile uint32_t pending_event_flags;
    volatile uint8_t  pending_event_pid;
    uint8_t           current_pid;
    uint32_t          last_poll_ms;
    volatile uint8_t  ok_tx_pending;
    volatile uint8_t  ok_token_pending;
    uint8_t           rx_buffer[8];
    uint8_t           tx_buffer[8];
    LinStatusFrame latest_status;
    LinStatusFrame slave_status_cache;
};

#endif
