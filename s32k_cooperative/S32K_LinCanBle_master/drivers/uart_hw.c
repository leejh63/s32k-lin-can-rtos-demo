// IsoSdk UART 계층 위에 만든 하드웨어 바인딩 구현 파일이다.
// 바이트 단위 RX callback 처리는 여기서 맡고,
// line 조립과 정책 판단은 UartService에 넘긴다.
#include "uart_hw.h"

#include <stddef.h>

#include "../services/uart_service_internal.h"
#include "../platform/s32k_sdk/isosdk_uart.h"

// 단순 성공 여부를 UartHwStatus enum으로 바꾼다.
static UartHwStatus UartHw_StatusFromBool(uint8_t ok)
{
    return (ok != 0U) ? UART_HW_STATUS_OK : UART_HW_STATUS_ERROR;
}

// RX pending ring의 다음 위치를 계산한다.
static uint16_t UartHw_NextPendingIndex(uint16_t index)
{
    index++;
    if (index >= UART_RX_PENDING_SIZE)
    {
        index = 0U;
    }

    return index;
}

// ISR이 더 밀어 넣을 자리가 남아 있는지 확인한다.
static uint8_t UartHw_IsPendingFull(const UartRxPendingRing *ring)
{
    uint16_t next_tail;

    if (ring == NULL)
    {
        return 1U;
    }

    next_tail = UartHw_NextPendingIndex(ring->tail);
    return (next_tail == ring->head) ? 1U : 0U;
}

// RX callback에서 받은 바이트 하나를 pending ring에 적재한다.
// line 조립은 service 쪽으로 미루고,
// 여기서는 최소한의 capture만 맡는다.
static void UartHw_PushPendingByte(UartService *service, uint8_t rx_byte)
{
    UartRxPendingRing *ring;

    if (service == NULL)
    {
        return;
    }

    ring = &service->rx_pending;
    if (UartHw_IsPendingFull(ring) != 0U)
    {
        ring->overflow = 1U;
        ring->overflow_count++;
        return;
    }

    ring->buffer[ring->tail] = rx_byte;
    ring->tail = UartHw_NextPendingIndex(ring->tail);
}

// 첫 바이트 수신을 시작한다.
static UartHwStatus UartHw_StartReceiveByte(UartService *service)
{
    if (service == NULL)
    {
        return UART_HW_STATUS_ERROR;
    }

    return UartHw_StatusFromBool(IsoSdk_UartStartReceiveByte(service->instance,
                                                             &service->rx_byte));
}

// callback 이후 다음 바이트 수신을 이어서 건다.
static UartHwStatus UartHw_ContinueReceiveByte(UartService *service)
{
    if (service == NULL)
    {
        return UART_HW_STATUS_ERROR;
    }

    return UartHw_StatusFromBool(IsoSdk_UartContinueReceiveByte(service->instance,
                                                                &service->rx_byte));
}

// IsoSdk UART callback에서 pending ring 적재와 재수신 등록을 처리한다.
static void UartHw_OnIsoSdkEvent(void *context, uint8_t event_id)
{
    UartService  *service;
    UartHwStatus  hw_status;

    service = (UartService *)context;
    if (service == NULL)
    {
        return;
    }

    if (event_id == ISOSDK_UART_EVENT_RX_FULL)
    {
        UartHw_PushPendingByte(service, service->rx_byte);
        hw_status = UartHw_ContinueReceiveByte(service);
        if (hw_status != UART_HW_STATUS_OK)
        {
            service->error_flag = 1U;
            service->error_code = UART_ERROR_RX_DRIVER;
            service->error_count++;
        }
        return;
    }

    if (event_id == ISOSDK_UART_EVENT_ERROR)
    {
        service->error_flag = 1U;
        service->error_code = UART_ERROR_RX_DRIVER;
        service->error_count++;

        hw_status = UartHw_ContinueReceiveByte(service);
        if (hw_status != UART_HW_STATUS_OK)
        {
            service->error_count++;
        }
    }
}

// 지정된 UART 인스턴스를 초기화하고 첫 RX를 시작한다.
UartHwStatus UartHw_Init(UartService *service, uint32_t instance)
{
    if (service == NULL)
    {
        return UART_HW_STATUS_ERROR;
    }

    if (IsoSdk_UartIsSupported() == 0U)
    {
        return UART_HW_STATUS_ERROR;
    }

    service->instance = instance;
    if (IsoSdk_UartInit(service->instance, UartHw_OnIsoSdkEvent, service) == 0U)
    {
        return UART_HW_STATUS_ERROR;
    }

    return UartHw_StartReceiveByte(service);
}

// 기본 UART 인스턴스를 초기화하고 첫 RX를 시작한다.
UartHwStatus UartHw_InitDefault(UartService *service)
{
    return UartHw_Init(service, IsoSdk_UartGetDefaultInstance());
}

// 전송 버퍼 하나를 실제 UART 하드웨어 전송으로 넘긴다.
UartHwStatus UartHw_StartTransmit(UartService *service, const uint8_t *data, uint16_t length)
{
    if ((service == NULL) || (data == NULL) || (length == 0U))
    {
        return UART_HW_STATUS_ERROR;
    }

    return UartHw_StatusFromBool(IsoSdk_UartStartTransmit(service->instance, data, length));
}

// 현재 UART 전송 상태를 하드웨어 공용 상태로 변환해 반환한다.
UartHwStatus UartHw_GetTransmitStatus(UartService *service, uint32_t *bytes_remaining)
{
    IsoSdkUartTxState tx_state;

    if (service == NULL)
    {
        return UART_HW_STATUS_ERROR;
    }

    tx_state = IsoSdk_UartGetTransmitState(service->instance, bytes_remaining);
    if (tx_state == ISOSDK_UART_TX_STATE_DONE)
    {
        return UART_HW_STATUS_OK;
    }

    if (tx_state == ISOSDK_UART_TX_STATE_BUSY)
    {
        return UART_HW_STATUS_BUSY;
    }

    return UART_HW_STATUS_ERROR;
}

// 참고:
// RX callback이 바이트 적재와 재수신 등록까지 맡는 구조는 콘솔에는 잘 맞지만,
// 수신량이 훨씬 늘어나는 경우엔 pending ring 크기와 overflow 대응을 함께 다시 검토하는 것이 적절하다.
