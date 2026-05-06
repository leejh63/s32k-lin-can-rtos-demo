#ifndef UART_SERVICE_INTERNAL_H
#define UART_SERVICE_INTERNAL_H

#include "uart_service.h"

#include "../core/infra_queue.h"

typedef struct
{
    uint8_t           buffer[UART_RX_PENDING_SIZE];
    volatile uint16_t head;
    volatile uint16_t tail;
    volatile uint8_t  overflow;
    volatile uint32_t overflow_count;
} UartRxPendingRing;

typedef struct
{
    char     buffer[UART_RX_LINE_SIZE];
    uint16_t length;
    uint8_t  line_ready;
    uint8_t  overflow;
} UartLineBuffer;

typedef struct
{
    uint8_t  data[UART_TX_CHUNK_SIZE];
    uint16_t length;
} UartTxChunk;

typedef struct
{
    char       current_buffer[UART_TX_CHUNK_SIZE + 1U];
    uint16_t   current_length;
    uint8_t    busy;
    uint32_t   start_ms;
    uint32_t   timeout_ms;
    uint32_t   queue_full_count;
    uint32_t   dropped_request_count;
    InfraQueue queue;
    UartTxChunk queue_storage[UART_TX_QUEUE_SIZE];
} UartTxContext;

struct UartService
{
    uint8_t           initialized;
    uint32_t          instance;
    uint8_t           rx_byte;
    UartRxPendingRing rx_pending;
    UartLineBuffer    rx_line;
    UartTxContext     tx;
    uint8_t           error_flag;
    uint32_t          error_count;
    UartErrorCode     error_code;
};

#endif
