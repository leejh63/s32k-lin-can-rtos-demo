#ifndef ISOSDK_UART_H
#define ISOSDK_UART_H

#include <stdint.h>

typedef enum
{
    ISOSDK_UART_EVENT_RX_FULL = 0,
    ISOSDK_UART_EVENT_ERROR
} IsoSdkUartEventId;

typedef enum
{
    ISOSDK_UART_TX_STATE_DONE = 0,
    ISOSDK_UART_TX_STATE_BUSY,
    ISOSDK_UART_TX_STATE_ERROR
} IsoSdkUartTxState;

typedef void (*IsoSdkUartEventCallback)(void *context, uint8_t event_id);

// IsoSdk_Uart 지원 여부를 확인한다.
uint8_t           IsoSdk_UartIsSupported(void);
uint8_t           IsoSdk_UartIsSecondarySupported(void);
// IsoSdk_Uart 기본 인스턴스를 조회한다.
uint32_t          IsoSdk_UartGetDefaultInstance(void);
// IsoSdk_Uart 보조 인스턴스를 조회한다.
uint32_t          IsoSdk_UartGetSecondaryInstance(void);
uint8_t           IsoSdk_UartInit(uint32_t instance,
                                  IsoSdkUartEventCallback event_cb,
                                  void *event_context);
uint8_t           IsoSdk_UartStartReceiveByte(uint32_t instance, uint8_t *out_byte);
uint8_t           IsoSdk_UartContinueReceiveByte(uint32_t instance, uint8_t *io_byte);
// IsoSdk_Uart 송신을 시작한다.
uint8_t           IsoSdk_UartStartTransmit(uint32_t instance, const uint8_t *data, uint16_t length);
IsoSdkUartTxState IsoSdk_UartGetTransmitState(uint32_t instance, uint32_t *bytes_remaining);

#endif
