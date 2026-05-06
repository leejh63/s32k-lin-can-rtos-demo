// 저수준 UART 하드웨어 계층 위에서 동작하는 service 구현 파일이다.
// 수신 문자를 line 단위로 조립하고 출력 텍스트를 chunk로 분할하며,
// 송수신 오류를 service 수준 상태로 추적한다.
#include "uart_service_internal.h"

#include <stddef.h>
#include <string.h>

#include "../drivers/uart_hw.h"

#define UART_CHAR_BACKSPACE_1  ((uint8_t)'\b')
#define UART_CHAR_BACKSPACE_2  (0x7FU)

// service 수준 UART 오류를 기록한다.
// 오류 기록 규칙을 한곳에 모아 두면,
// 모든 실패 경로가 같은 flag, code, counter 의미를 유지할 수 있다.
static void UartService_SetError(UartService *service, UartErrorCode code)
{
    if (service == NULL)
    {
        return;
    }

    service->error_flag = 1U;
    service->error_code = code;
    service->error_count++;
}

// 버퍼링된 수신 상태를 모두 초기화한다.
// 초기화 직후, 복구 시점, overflow 처리 경로에서 사용하여,
// 다음 line이 일관된 파서 상태에서 시작되게 한다.
static void UartService_ResetRx(UartService *service)
{
    if (service == NULL)
    {
        return;
    }

    service->rx_pending.head = 0U;
    service->rx_pending.tail = 0U;
    service->rx_pending.overflow = 0U;
    service->rx_pending.overflow_count = 0U;

    service->rx_line.length = 0U;
    service->rx_line.line_ready = 0U;
    service->rx_line.overflow = 0U;
    service->rx_line.buffer[0] = '\0';
}

// 전송 상태와 queue를 초기 상태로 복원한다.
static InfraStatus UartService_ResetTx(UartService *service)
{
    if (service == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    service->tx.current_length = 0U;
    service->tx.current_buffer[0] = '\0';
    service->tx.busy = 0U;
    service->tx.start_ms = 0U;
    service->tx.timeout_ms = UART_DEFAULT_TIMEOUT_MS;

    return InfraQueue_Init(&service->tx.queue,
                           service->tx.queue_storage,
                           (uint16_t)sizeof(UartTxChunk),
                           UART_TX_QUEUE_SIZE);
}

// UART service 전체 상태를 기본값으로 초기화한다.
static void UartService_Reset(UartService *service)
{
    if (service == NULL)
    {
        return;
    }

    (void)memset(service, 0, sizeof(*service));
    UartService_ResetRx(service);
    if (UartService_ResetTx(service) != INFRA_STATUS_OK)
    {
        UartService_SetError(service, UART_ERROR_TX_DRIVER);
    }
}

// RX pending ring의 다음 위치를 계산한다.
static uint16_t UartService_NextPendingIndex(uint16_t index)
{
    index++;
    if (index >= UART_RX_PENDING_SIZE)
    {
        index = 0U;
    }

    return index;
}

// ISR이 적재한 pending 바이트가 비어 있는지 확인한다.
static uint8_t UartService_IsPendingEmpty(const UartService *service)
{
    if (service == NULL)
    {
        return 1U;
    }

    return (service->rx_pending.head == service->rx_pending.tail) ? 1U : 0U;
}

// pending ring에서 바이트 하나를 꺼내 task 문맥으로 전달한다.
// ISR과 task가 공유하는 ring이므로 head 갱신 구간의 동시성 특성을 고려해야 한다.
static InfraStatus UartService_PopPendingByte(UartService *service, uint8_t *out_byte)
{
    if ((service == NULL) || (out_byte == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (UartService_IsPendingEmpty(service) != 0U)
    {
        return INFRA_STATUS_EMPTY;
    }

    *out_byte = service->rx_pending.buffer[service->rx_pending.head];
    service->rx_pending.head = UartService_NextPendingIndex(service->rx_pending.head);
    return INFRA_STATUS_OK;
}

// 수신 바이트 하나를 현재 line buffer에 반영한다.
// newline 종료, backspace 편집,
// printable 필터링과 overflow 감지를 이 함수에서 함께 처리한다.
static void UartService_OnRxByte(UartService *service, uint8_t rx_byte)
{
    UartLineBuffer *line;

    if (service == NULL)
    {
        return;
    }

    line = &service->rx_line;
    if (line->line_ready != 0U)
    {
        return;
    }

    if ((rx_byte == '\r') || (rx_byte == '\n'))
    {
        if (line->length < UART_RX_LINE_SIZE)
        {
            line->buffer[line->length] = '\0';
        }
        else
        {
            line->buffer[UART_RX_LINE_SIZE - 1U] = '\0';
        }

        line->line_ready = 1U;
        return;
    }

    if ((rx_byte == UART_CHAR_BACKSPACE_1) || (rx_byte == UART_CHAR_BACKSPACE_2))
    {
        if (line->length > 0U)
        {
            line->length--;
            line->buffer[line->length] = '\0';
        }
        return;
    }

    if ((rx_byte < 32U) || (rx_byte > 126U))
    {
        return;
    }

    if (line->length >= (UART_RX_LINE_SIZE - 1U))
    {
        line->overflow = 1U;
        line->buffer[UART_RX_LINE_SIZE - 1U] = '\0';
        line->line_ready = 1U;
        return;
    }

    line->buffer[line->length] = (char)rx_byte;
    line->length++;
    line->buffer[line->length] = '\0';
}

// 임의의 바이트 시퀀스를 UART 전송 queue에 적재한다.
// 필요한 chunk 수를 먼저 확인한 뒤에만 push하여,
// 일부 chunk만 들어가고 실패하는 중간 상태를 만들지 않는다.
static InfraStatus UartService_RequestTxBytes(UartService *service,
                                              const uint8_t *data,
                                              uint16_t length)
{
    UartTxChunk  chunk;
    uint16_t     offset;
    uint16_t     chunk_length;
    uint16_t     required_chunks;
    uint16_t     queued_chunks;
    uint16_t     capacity_chunks;
    uint16_t     available_chunks;
    InfraStatus  status;

    if ((service == NULL) || (data == NULL) || (length == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    required_chunks = (uint16_t)((length + UART_TX_CHUNK_SIZE - 1U) / UART_TX_CHUNK_SIZE);
    queued_chunks = InfraQueue_GetCount(&service->tx.queue);
    capacity_chunks = InfraQueue_GetCapacity(&service->tx.queue);
    if (queued_chunks >= capacity_chunks)
    {
        service->tx.queue_full_count++;
        service->tx.dropped_request_count++;
        return INFRA_STATUS_FULL;
    }

    available_chunks = (uint16_t)(capacity_chunks - queued_chunks);
    if (available_chunks < required_chunks)
    {
        service->tx.queue_full_count++;
        service->tx.dropped_request_count++;
        return INFRA_STATUS_FULL;
    }

    offset = 0U;
    while (offset < length)
    {
        chunk_length = (uint16_t)(length - offset);
        if (chunk_length > UART_TX_CHUNK_SIZE)
        {
            chunk_length = UART_TX_CHUNK_SIZE;
        }

        (void)memset(&chunk, 0, sizeof(chunk));
        (void)memcpy(chunk.data, &data[offset], chunk_length);
        chunk.length = chunk_length;

        status = InfraQueue_Push(&service->tx.queue, &chunk);
        if (status != INFRA_STATUS_OK)
        {
            service->tx.queue_full_count++;
            service->tx.dropped_request_count++;
            return status;
        }

        offset = (uint16_t)(offset + chunk_length);
    }

    return INFRA_STATUS_OK;
}

// UART service와 저수준 하드웨어 바인딩을 초기화한다.
// 먼저 소프트웨어 버퍼를 초기화하여,
// 하드웨어 초기화 실패 시에도 service가 정의된 진단 상태를 유지하게 한다.
InfraStatus UartService_Init(UartService *service)
{
    UartHwStatus status;

    if (service == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    UartService_Reset(service);
    if (service->error_flag != 0U)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    status = UartHw_InitDefault(service);
    if (status != UART_HW_STATUS_OK)
    {
        UartService_SetError(service, UART_ERROR_HW_INIT);
        return INFRA_STATUS_IO_ERROR;
    }

    service->initialized = 1U;
    return INFRA_STATUS_OK;
}

InfraStatus UartService_InitWithInstance(UartService *service, uint32_t instance)
{
    UartHwStatus status;

    if (service == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    UartService_Reset(service);
    if (service->error_flag != 0U)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    status = UartHw_Init(service, instance);
    if (status != UART_HW_STATUS_OK)
    {
        UartService_SetError(service, UART_ERROR_HW_INIT);
        return INFRA_STATUS_IO_ERROR;
    }

    service->initialized = 1U;
    return INFRA_STATUS_OK;
}

// 치명적인 UART 오류 이후 service를 다시 초기화한다.
// console은 전체 프로그램을 다시 시작하는 대신,
// 수동 `recover` 명령에서 이 경로를 사용한다.
InfraStatus UartService_Recover(UartService *service)
{
    uint32_t instance;

    if (service == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    instance = service->instance;
    return UartService_InitWithInstance(service, instance);
}

// pending RX 바이트를 비우며 다음 완성 line을 만든다.
// ISR 작업량은 작게 유지하면서도,
// line parsing은 일반 task 문맥에서 수행되게 한다.
void UartService_ProcessRx(UartService *service)
{
    uint8_t rx_byte;

    if ((service == NULL) || (service->initialized == 0U))
    {
        return;
    }

    if (service->rx_pending.overflow != 0U)
    {
        UartService_ResetRx(service);
        UartService_SetError(service, UART_ERROR_RX_PENDING_OVERFLOW);
        return;
    }

    while (UartService_PopPendingByte(service, &rx_byte) == INFRA_STATUS_OK)
    {
        UartService_OnRxByte(service, rx_byte);

        if (service->rx_line.overflow != 0U)
        {
            UartService_ResetRx(service);
            UartService_SetError(service, UART_ERROR_RX_LINE_OVERFLOW);
            break;
        }

        if (service->rx_line.line_ready != 0U)
        {
            break;
        }
    }
}

// 현재 transmit 작업을 진행시키거나 다음 작업을 시작한다.
// timeout 감지도 여기서 수행하여,
// 스케줄러를 막지 않고 멈춘 전송을 감지할 수 있게 한다.
void UartService_ProcessTx(UartService *service, uint32_t now_ms)
{
    InfraStatus status;
    UartTxChunk chunk;
    uint32_t    bytes_remaining;
    UartHwStatus    hw_status;

    if ((service == NULL) || (service->initialized == 0U))
    {
        return;
    }

    if (service->tx.busy == 0U)
    {
        status = InfraQueue_Peek(&service->tx.queue, &chunk);
        if (status == INFRA_STATUS_EMPTY)
        {
            return;
        }

        if (status != INFRA_STATUS_OK)
        {
            UartService_SetError(service, UART_ERROR_TX_DRIVER);
            return;
        }

        if (chunk.length == 0U)
        {
            (void)InfraQueue_Pop(&service->tx.queue, &chunk);
            return;
        }

        (void)memcpy(service->tx.current_buffer, chunk.data, chunk.length);
        service->tx.current_buffer[chunk.length] = '\0';
        service->tx.current_length = chunk.length;

        hw_status = UartHw_StartTransmit(service,
                                         (const uint8_t *)service->tx.current_buffer,
                                         service->tx.current_length);
        if (hw_status != UART_HW_STATUS_OK)
        {
            service->tx.current_length = 0U;
            service->tx.current_buffer[0] = '\0';
            UartService_SetError(service, UART_ERROR_TX_DRIVER);
            return;
        }

        service->tx.busy = 1U;
        service->tx.start_ms = now_ms;
        status = InfraQueue_Pop(&service->tx.queue, &chunk);
        if (status != INFRA_STATUS_OK)
        {
            UartService_SetError(service, UART_ERROR_TX_DRIVER);
        }
        return;
    }

    if (Infra_TimeIsExpired(now_ms, service->tx.start_ms, service->tx.timeout_ms) != 0U)
    {
        service->tx.busy = 0U;
        service->tx.current_length = 0U;
        service->tx.current_buffer[0] = '\0';
        UartService_SetError(service, UART_ERROR_TX_TIMEOUT);
        return;
    }

    bytes_remaining = 0U;
    hw_status = UartHw_GetTransmitStatus(service, &bytes_remaining);
    if ((hw_status == UART_HW_STATUS_OK) && (bytes_remaining == 0U))
    {
        service->tx.busy = 0U;
        service->tx.current_length = 0U;
        service->tx.current_buffer[0] = '\0';
        return;
    }

    if (hw_status == UART_HW_STATUS_BUSY)
    {
        return;
    }

    if (hw_status != UART_HW_STATUS_OK)
    {
        service->tx.busy = 0U;
        service->tx.current_length = 0U;
        service->tx.current_buffer[0] = '\0';
        UartService_SetError(service, UART_ERROR_TX_DRIVER);
    }
}

// NUL 종료 문자열 하나를 전송 queue에 적재한다.
// 상위 console 계층은 모든 terminal 갱신에서,
// 일반 렌더링과 메시지 출력 경로로 이 함수를 사용한다.
InfraStatus UartService_RequestTx(UartService *service, const char *text)
{
    uint16_t length;

    if ((service == NULL) || (text == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    length = (uint16_t)strlen(text);
    if (length == 0U)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    return UartService_RequestTxBytes(service, (const uint8_t *)text, length);
}

// 완성된 입력 한 줄이 준비됐는지 확인한다.
uint8_t UartService_HasLine(const UartService *service)
{
    if (service == NULL)
    {
        return 0U;
    }

    return service->rx_line.line_ready;
}

// 현재 완성된 line을 호출자 버퍼로 복사한다.
// line을 읽어가면 RX 조립 상태도 함께 reset되어,
// 다음 operator 명령이 빈 buffer에서 시작된다.
InfraStatus UartService_ReadLine(UartService *service, char *out_buffer, uint16_t max_length)
{
    uint16_t copy_length;

    if ((service == NULL) || (out_buffer == NULL) || (max_length == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (service->rx_line.line_ready == 0U)
    {
        return INFRA_STATUS_EMPTY;
    }

    copy_length = service->rx_line.length;
    if (copy_length >= max_length)
    {
        copy_length = (uint16_t)(max_length - 1U);
    }

    if (copy_length > 0U)
    {
        (void)memcpy(out_buffer, service->rx_line.buffer, copy_length);
    }

    out_buffer[copy_length] = '\0';
    UartService_ResetRx(service);
    return INFRA_STATUS_OK;
}

// 아직 엔터를 치지 않은 현재 입력 내용을 화면 갱신용으로 복사한다.
InfraStatus UartService_GetCurrentInputText(const UartService *service,
                                            char *out_buffer,
                                            uint16_t max_length)
{
    uint16_t copy_length;

    if ((service == NULL) || (out_buffer == NULL) || (max_length == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    copy_length = service->rx_line.length;
    if (copy_length >= max_length)
    {
        copy_length = (uint16_t)(max_length - 1U);
    }

    if (copy_length > 0U)
    {
        (void)memcpy(out_buffer, service->rx_line.buffer, copy_length);
    }

    out_buffer[copy_length] = '\0';
    return INFRA_STATUS_OK;
}

// 현재 입력 중인 문자열 길이를 반환한다.
uint16_t UartService_GetCurrentInputLength(const UartService *service)
{
    if (service == NULL)
    {
        return 0U;
    }

    return service->rx_line.length;
}

// 아직 보내지지 않은 TX chunk 개수를 알려준다.
uint16_t UartService_GetTxQueueCount(const UartService *service)
{
    if (service == NULL)
    {
        return 0U;
    }

    return InfraQueue_GetCount(&service->tx.queue);
}

// TX queue 전체 용량을 알려준다.
uint16_t UartService_GetTxQueueCapacity(const UartService *service)
{
    if (service == NULL)
    {
        return 0U;
    }

    return InfraQueue_GetCapacity(&service->tx.queue);
}

// 현재 전송이 진행 중인지 확인한다.
uint8_t UartService_IsTxBusy(const UartService *service)
{
    if (service == NULL)
    {
        return 0U;
    }

    return service->tx.busy;
}

// UART service가 오류 상태에 들어갔는지 확인한다.
uint8_t UartService_HasError(const UartService *service)
{
    if (service == NULL)
    {
        return 1U;
    }

    return service->error_flag;
}

// 마지막으로 기록된 UART 오류 코드를 반환한다.
UartErrorCode UartService_GetErrorCode(const UartService *service)
{
    if (service == NULL)
    {
        return UART_ERROR_HW_INIT;
    }

    return service->error_code;
}

// 참고:
// recover 경로까지 갖춘 구조라 쓰기 편하지만, 에러가 누적됐을 때 어떤 카운터를 유지하고 무엇을 리셋할지는
// 운영 기준에 맞춰 연결 종료 정책을 명확히 분리할 수 있다.
