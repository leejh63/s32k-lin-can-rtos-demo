// 저수준 UART 하드웨어 바인딩 인터페이스다.
// service 계층은 이 호출들을 사용해 전송을 시작하고,
// IsoSdk 계층 위에서 RX/TX 상태를 확인한다.
#ifndef UART_HW_H
#define UART_HW_H

#include <stdint.h>

#include "../services/uart_types.h"

typedef enum
{
    UART_HW_STATUS_OK = 0,
    UART_HW_STATUS_BUSY,
    UART_HW_STATUS_ERROR
} UartHwStatus;

UartHwStatus UartHw_Init(UartService *service, uint32_t instance);
UartHwStatus UartHw_InitDefault(UartService *service);
UartHwStatus UartHw_StartTransmit(UartService *service, const uint8_t *data, uint16_t length);
UartHwStatus UartHw_GetTransmitStatus(UartService *service, uint32_t *bytes_remaining);

#endif
