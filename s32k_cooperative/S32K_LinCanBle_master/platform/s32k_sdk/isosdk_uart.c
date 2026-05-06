// LPUART generated driver를 얇게 감싼 구현 파일이다.
// 상위 계층은 byte 단위 RX/TX와 event만 보고,
// SDK callback 타입과 상태 코드는 이 파일이 대신 정리한다.
#include "isosdk_uart.h"

#include <stddef.h>

#include "lpuart_driver.h"

#include "isosdk_sdk_bindings.h"

#ifdef ISOSDK_SDK_HAS_UART

static IsoSdkUartEventCallback s_iso_sdk_uart_event_cb[LPUART_INSTANCE_COUNT];
static void                   *s_iso_sdk_uart_event_context[LPUART_INSTANCE_COUNT];

#ifdef ISOSDK_SDK_HAS_UART_SECONDARY
static lpuart_state_t s_iso_sdk_uart_secondary_state;
static const lpuart_user_config_t s_iso_sdk_uart_secondary_init_config =
{
    .transferType = LPUART_USING_INTERRUPTS,
    .baudRate = 9600UL,
    .parityMode = LPUART_PARITY_DISABLED,
    .stopBitCount = LPUART_ONE_STOP_BIT,
    .bitCountPerChar = LPUART_8_BITS_PER_CHAR,
    .rxDMAChannel = 0UL,
    .txDMAChannel = 0UL
};
#endif

static uint8_t IsoSdk_UartIsInstanceInRange(uint32_t instance)
{
    return (instance < (uint32_t)LPUART_INSTANCE_COUNT) ? 1U : 0U;
}

static uint8_t IsoSdk_UartResolveConfig(uint32_t instance,
                                        lpuart_state_t **out_state,
                                        const lpuart_user_config_t **out_config)
{
    if ((out_state == NULL) || (out_config == NULL))
    {
        return 0U;
    }

    if (instance == ISOSDK_SDK_UART_INSTANCE)
    {
        *out_state = &ISOSDK_SDK_UART_STATE;
        *out_config = &ISOSDK_SDK_UART_INIT_CONFIG;
        return 1U;
    }

#ifdef ISOSDK_SDK_HAS_UART_SECONDARY
    if (instance == ISOSDK_SDK_UART_SECONDARY_INSTANCE)
    {
        *out_state = &s_iso_sdk_uart_secondary_state;
        *out_config = &s_iso_sdk_uart_secondary_init_config;
        return 1U;
    }
#endif

    return 0U;
}

// SDK RX callback을 공용 UART 이벤트로 바꿔 상위로 전달한다.
static void IsoSdk_UartRxCallback(void *driver_state, uart_event_t event, void *user_data)
{
    uint32_t instance;

    (void)driver_state;
    instance = (uint32_t)(uintptr_t)user_data;

    if ((IsoSdk_UartIsInstanceInRange(instance) == 0U) ||
        (s_iso_sdk_uart_event_cb[instance] == NULL))
    {
        return;
    }

    if (event == UART_EVENT_RX_FULL)
    {
        s_iso_sdk_uart_event_cb[instance](s_iso_sdk_uart_event_context[instance],
                                          ISOSDK_UART_EVENT_RX_FULL);
        return;
    }

    if (event == UART_EVENT_ERROR)
    {
        s_iso_sdk_uart_event_cb[instance](s_iso_sdk_uart_event_context[instance],
                                          ISOSDK_UART_EVENT_ERROR);
    }
}

// 현재 빌드에서 UART를 사용할 수 있는지 알려준다.
uint8_t IsoSdk_UartIsSupported(void)
{
    return 1U;
}

uint8_t IsoSdk_UartIsSecondarySupported(void)
{
#ifdef ISOSDK_SDK_HAS_UART_SECONDARY
    return 1U;
#else
    return 0U;
#endif
}

// generated 설정이 선택한 기본 UART 인스턴스를 반환한다.
uint32_t IsoSdk_UartGetDefaultInstance(void)
{
    return ISOSDK_SDK_UART_INSTANCE;
}

uint32_t IsoSdk_UartGetSecondaryInstance(void)
{
#ifdef ISOSDK_SDK_HAS_UART_SECONDARY
    return ISOSDK_SDK_UART_SECONDARY_INSTANCE;
#else
    return 0U;
#endif
}

// UART controller를 초기화하고 RX callback을 연결한다.
// 이후 상위 계층은 byte 송수신 API만 사용하면 된다.
uint8_t IsoSdk_UartInit(uint32_t instance,
                        IsoSdkUartEventCallback event_cb,
                        void *event_context)
{
    status_t                     status;
    lpuart_state_t              *driver_state;
    const lpuart_user_config_t  *driver_config;

    if ((IsoSdk_UartIsInstanceInRange(instance) == 0U) ||
        (IsoSdk_UartResolveConfig(instance, &driver_state, &driver_config) == 0U))
    {
        return 0U;
    }

    s_iso_sdk_uart_event_cb[instance] = event_cb;
    s_iso_sdk_uart_event_context[instance] = event_context;

    status = LPUART_DRV_Init(instance,
                             driver_state,
                             driver_config);
    if (status != STATUS_SUCCESS)
    {
        s_iso_sdk_uart_event_cb[instance] = NULL;
        s_iso_sdk_uart_event_context[instance] = NULL;
        return 0U;
    }

    (void)LPUART_DRV_InstallRxCallback(instance,
                                       IsoSdk_UartRxCallback,
                                       (void *)(uintptr_t)instance);
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

uint8_t IsoSdk_UartIsSecondarySupported(void)
{
    return 0U;
}

// 미지원 빌드용 기본값이다.
uint32_t IsoSdk_UartGetDefaultInstance(void)
{
    return 0U;
}

uint32_t IsoSdk_UartGetSecondaryInstance(void)
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
