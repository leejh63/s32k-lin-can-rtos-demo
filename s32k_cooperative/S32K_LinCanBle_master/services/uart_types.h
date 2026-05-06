// service 계층이 공유하는 UART 전송 자료구조 모음이다.
// 이 헤더의 타입들은 RX 버퍼링과 TX queueing,
// 그리고 콘솔 모듈이 들고 있는 전체 service 상태를 표현한다.
#ifndef UART_TYPES_H
#define UART_TYPES_H

#include <stdint.h>

#define UART_RX_LINE_SIZE            64U
#define UART_RX_PENDING_SIZE         32U
#define UART_TX_CHUNK_SIZE           128U
#define UART_TX_QUEUE_SIZE           8U
#define UART_DEFAULT_TIMEOUT_MS      300U

typedef enum
{
    UART_ERROR_NONE = 0,
    UART_ERROR_HW_INIT,
    UART_ERROR_RX_DRIVER,
    UART_ERROR_RX_PENDING_OVERFLOW,
    UART_ERROR_RX_LINE_OVERFLOW,
    UART_ERROR_TX_QUEUE_FULL,
    UART_ERROR_TX_DRIVER,
    UART_ERROR_TX_TIMEOUT
} UartErrorCode;

typedef struct UartService UartService;

#endif
