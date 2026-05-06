// LPUART generated driver를 얇게 감싼 구현 파일이다.
// 상위 계층은 byte 단위 RX/TX와 event만 보고,
// SDK callback 타입과 상태 코드는 이 파일이 대신 정리한다.
#include "isosdk_uart.h"

#include <stddef.h>

#include "lpuart_driver.h"

#include "isosdk_sdk_bindings.h"

#ifdef ISOSDK_SDK_HAS_UART

static IsoSdkUartEventCallback s_iso_sdk_uart_event_cb;
static void                   *s_iso_sdk_uart_event_context;

// SDK RX callback을 공용 UART 이벤트로 바꿔 상위로 전달한다.
static void IsoSdk_UartRxCallback(void *driver_state, uart_event_t event, void *user_data)
{
    (void)driver_state;
    (void)user_data;

    if (s_iso_sdk_uart_event_cb == NULL)
    {
        return;
    }

    if (event == UART_EVENT_RX_FULL)
    {
        s_iso_sdk_uart_event_cb(s_iso_sdk_uart_event_context, ISOSDK_UART_EVENT_RX_FULL);
        return;
    }

    if (event == UART_EVENT_ERROR)
    {
        s_iso_sdk_uart_event_cb(s_iso_sdk_uart_event_context, ISOSDK_UART_EVENT_ERROR);
    }
}

// 현재 빌드에서 UART를 사용할 수 있는지 알려준다.
uint8_t IsoSdk_UartIsSupported(void)
{
    return 1U;
}

// generated 설정이 선택한 기본 UART 인스턴스를 반환한다.
uint32_t IsoSdk_UartGetDefaultInstance(void)
{
    return ISOSDK_SDK_UART_INSTANCE;
}

// UART controller를 초기화하고 RX callback을 연결한다.
uint8_t IsoSdk_UartInit(uint32_t instance,
                        IsoSdkUartEventCallback event_cb,
                        void *event_context)
{
    status_t status;

    s_iso_sdk_uart_event_cb = event_cb;
    s_iso_sdk_uart_event_context = event_context;

    status = LPUART_DRV_Init(instance,
                             &ISOSDK_SDK_UART_STATE,
                             &ISOSDK_SDK_UART_INIT_CONFIG);
    if (status != STATUS_SUCCESS)
    {
        return 0U;
    }

    (void)LPUART_DRV_InstallRxCallback(instance, IsoSdk_UartRxCallback, NULL);
    return 1U;
}

// 바이트 하나를 받을 첫 RX 버퍼를 등록한다.
uint8_t IsoSdk_UartStartReceiveByte(uint32_t instance, uint8_t *out_byte)
{
    if (out_byte == NULL)
    {
        return 0U;
    }

    *out_byte = 0U;
    return (LPUART_DRV_ReceiveData(instance, out_byte, 1U) == STATUS_SUCCESS) ? 1U : 0U;
}

// callback 이후 다음 바이트를 이어서 받을 RX 버퍼를 다시 건다.
uint8_t IsoSdk_UartContinueReceiveByte(uint32_t instance, uint8_t *io_byte)
{
    if (io_byte == NULL)
    {
        return 0U;
    }

    *io_byte = 0U;
    return (LPUART_DRV_SetRxBuffer(instance, io_byte, 1U) == STATUS_SUCCESS) ? 1U : 0U;
}

// 준비된 버퍼를 비동기 UART 전송으로 시작한다.
uint8_t IsoSdk_UartStartTransmit(uint32_t instance, const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U))
    {
        return 0U;
    }

    return (LPUART_DRV_SendData(instance, data, (uint32_t)length) == STATUS_SUCCESS) ? 1U : 0U;
}

// 전송 진행 상태를 공용 enum으로 정리해 반환한다.
IsoSdkUartTxState IsoSdk_UartGetTransmitState(uint32_t instance, uint32_t *bytes_remaining)
{
    status_t status;
    uint32_t remaining;

    remaining = 0U;
    status = LPUART_DRV_GetTransmitStatus(instance, &remaining);
    if (bytes_remaining != NULL)
    {
        *bytes_remaining = remaining;
    }

    if ((status == STATUS_SUCCESS) && (remaining == 0U))
    {
        return ISOSDK_UART_TX_STATE_DONE;
    }

    if ((status == STATUS_SUCCESS) || (status == STATUS_BUSY))
    {
        return ISOSDK_UART_TX_STATE_BUSY;
    }

    return ISOSDK_UART_TX_STATE_ERROR;
}

#else

// UART가 빠진 빌드에서는 미지원 상태를 반환한다.
uint8_t IsoSdk_UartIsSupported(void)
{
    return 0U;
}

// 미지원 빌드용 기본값이다.
uint32_t IsoSdk_UartGetDefaultInstance(void)
{
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_UartInit(uint32_t instance,
                        IsoSdkUartEventCallback event_cb,
                        void *event_context)
{
    (void)instance;
    (void)event_cb;
    (void)event_context;
    return 0U;
}

// 미지원 빌드에서는 RX 시작 요청을 실패로 반환한다.
uint8_t IsoSdk_UartStartReceiveByte(uint32_t instance, uint8_t *out_byte)
{
    (void)instance;
    (void)out_byte;
    return 0U;
}

// 미지원 빌드에서는 이어받기도 실패로 반환한다.
uint8_t IsoSdk_UartContinueReceiveByte(uint32_t instance, uint8_t *io_byte)
{
    (void)instance;
    (void)io_byte;
    return 0U;
}

// 미지원 빌드에서는 전송 요청을 처리하지 않는다.
uint8_t IsoSdk_UartStartTransmit(uint32_t instance, const uint8_t *data, uint16_t length)
{
    (void)instance;
    (void)data;
    (void)length;
    return 0U;
}

// 미지원 빌드에서는 항상 error 상태를 반환한다.
IsoSdkUartTxState IsoSdk_UartGetTransmitState(uint32_t instance, uint32_t *bytes_remaining)
{
    (void)instance;

    if (bytes_remaining != NULL)
    {
        *bytes_remaining = 0U;
    }

    return ISOSDK_UART_TX_STATE_ERROR;
}

#endif

// 참고:
// 현재 RX 흐름은 byte 단위 callback을 전제로 하고 있어서 콘솔 입력에는 잘 맞지만,
// 대용량 데이터 스트림을 받을 일이 생기면 버퍼 전략을 조금 더 크게 가져가는 편이 효율적이다.
