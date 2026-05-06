// pending request를 관리하는 CAN service 구현 파일이다.
// request ID를 추적하고 수신 프레임을 해석하며,
// timeout 및 response 결과를 상위 계층에 제공한다.
#include "can_service.h"

#include <stddef.h>
#include <string.h>

// reset 보조 함수를 한곳에 모아 queue와 pending-table 처리 코드를 단순화한다.
// 초기화 규칙을 중앙화하면,
// service 내부에서 동일한 memset 로직을 반복하지 않아도 된다.
static void CanService_ClearMessage(CanMessage *message)
{
    if (message != NULL)
    {
        (void)memset(message, 0, sizeof(*message));
    }
}

// request 완료 결과 저장 공간을 기본 상태로 초기화한다.
static void CanService_ClearResult(CanServiceResult *result)
{
    if (result != NULL)
    {
        (void)memset(result, 0, sizeof(*result));
    }
}

// pending request 슬롯 하나를 빈 상태로 초기화한다.
static void CanService_ClearPending(CanPendingRequest *pending)
{
    if (pending != NULL)
    {
        (void)memset(pending, 0, sizeof(*pending));
    }
}

// 고정 크기 queue에서 다음 슬롯 번호를 계산한다.
static uint8_t CanService_NextIndex(uint8_t index, uint8_t capacity)
{
    index++;
    if (index >= capacity)
    {
        index = 0U;
    }

    return index;
}

// 논리 target으로 허용되는 node id인지 확인한다.
static uint8_t CanService_IsValidTarget(uint8_t node_id)
{
    if (node_id == CAN_NODE_ID_BROADCAST)
    {
        return 1U;
    }

    return ((node_id >= CAN_NODE_ID_MIN) && (node_id <= CAN_NODE_ID_MAX)) ? 1U : 0U;
}

// 현재 로컬 노드가 수신 대상으로 받아야 할 target인지 확인한다.
static uint8_t CanService_IsAcceptedTarget(const CanService *service, uint8_t target_node_id)
{
    if (service == NULL)
    {
        return 0U;
    }

    if (target_node_id == service->local_node_id)
    {
        return 1U;
    }

    return (target_node_id == CAN_NODE_ID_BROADCAST) ? 1U : 0U;
}

// text 메시지가 protocol 제약을 만족하는지 검사한다.
static uint8_t CanService_IsPrintableAscii(const char *text)
{
    unsigned char ch;

    if ((text == NULL) || (text[0] == '\0'))
    {
        return 0U;
    }

    while (*text != '\0')
    {
        ch = (unsigned char)*text;
        if ((ch < 32U) || (ch > 126U))
        {
            return 0U;
        }
        text++;
    }

    return 1U;
}

// transport 내부 frame 저장 공간을 초기화한다.
static void CanTransport_ClearFrame(CanFrame *frame)
{
    if (frame != NULL)
    {
        (void)memset(frame, 0, sizeof(*frame));
    }
}

// TX queue가 가득 찼는지 확인한다.
static uint8_t CanTransport_TxIsFull(const CanTransport *transport)
{
    return (transport->tx_count >= CAN_TRANSPORT_TX_QUEUE_SIZE) ? 1U : 0U;
}

// RX queue가 가득 찼는지 확인한다.
static uint8_t CanTransport_RxIsFull(const CanTransport *transport)
{
    return (transport->rx_count >= CAN_TRANSPORT_RX_QUEUE_SIZE) ? 1U : 0U;
}

// 다음으로 보낼 frame을 제거하지 않고 미리 본다.
static uint8_t CanTransport_TxPeek(const CanTransport *transport, CanFrame *out_frame)
{
    if ((transport == NULL) || (out_frame == NULL) || (transport->tx_count == 0U))
    {
        return 0U;
    }

    *out_frame = transport->tx_queue[transport->tx_head];
    return 1U;
}

// 전송이 끝난 frame 하나를 TX queue 앞에서 제거한다.
static void CanTransport_TxDropFront(CanTransport *transport)
{
    if ((transport == NULL) || (transport->tx_count == 0U))
    {
        return;
    }

    CanTransport_ClearFrame(&transport->tx_queue[transport->tx_head]);
    transport->tx_head = CanService_NextIndex(transport->tx_head, CAN_TRANSPORT_TX_QUEUE_SIZE);
    transport->tx_count--;
}

// 하드웨어 카운터 변화를 기준으로 전송 완료를 transport 상태에 반영한다.
static void CanTransport_OnTxComplete(CanTransport *transport)
{
    if (transport == NULL)
    {
        return;
    }

    if (transport->hw.tx_ok_count != transport->hw_tx_ok_count_seen)
    {
        transport->hw_tx_ok_count_seen = transport->hw.tx_ok_count;
        transport->tx_in_flight = 0U;
        CanTransport_TxDropFront(transport);
        CanTransport_ClearFrame(&transport->current_tx_frame);
        return;
    }

    if (transport->hw.tx_error_count != transport->hw_tx_error_count_seen)
    {
        transport->hw_tx_error_count_seen = transport->hw.tx_error_count;
        transport->tx_in_flight = 0U;
        transport->last_error = CAN_TRANSPORT_ERROR_HW_TX_FAIL;
        CanTransport_TxDropFront(transport);
        CanTransport_ClearFrame(&transport->current_tx_frame);
        return;
    }

    transport->tx_in_flight = 0U;
    transport->last_error = CAN_TRANSPORT_ERROR_HW_TX_FAIL;
    CanTransport_TxDropFront(transport);
    CanTransport_ClearFrame(&transport->current_tx_frame);
}

// 하드웨어에서 꺼낸 frame을 transport RX queue에 적재한다.
static uint8_t CanTransport_RxPush(CanTransport *transport, const CanFrame *frame)
{
    if ((transport == NULL) || (frame == NULL))
    {
        return 0U;
    }

    if (CanTransport_RxIsFull(transport) != 0U)
    {
        return 0U;
    }

    transport->rx_queue[transport->rx_tail] = *frame;
    transport->rx_tail = CanService_NextIndex(transport->rx_tail, CAN_TRANSPORT_RX_QUEUE_SIZE);
    transport->rx_count++;
    return 1U;
}

// 하드웨어 RX queue의 frame을 software RX queue로 이동한다.
static void CanTransport_DrainHwRx(CanTransport *transport)
{
    CanFrame frame;

    if (transport == NULL)
    {
        return;
    }

    while (CanHw_TryPopRx(&transport->hw, &frame) != 0U)
    {
        if (CanTransport_RxPush(transport, &frame) == 0U)
        {
            transport->last_error = CAN_TRANSPORT_ERROR_RX_QUEUE_FULL;
        }
    }
}

// 진행 중인 TX를 정리하거나 다음 frame 전송을 시작한다.
static void CanTransport_ProcessTx(CanTransport *transport)
{
    CanFrame frame;

    if (transport == NULL)
    {
        return;
    }

    if (transport->tx_in_flight != 0U)
    {
        if (CanHw_IsTxBusy(&transport->hw) != 0U)
        {
            return;
        }

        CanTransport_OnTxComplete(transport);
    }

    if (transport->tx_count == 0U)
    {
        return;
    }

    if (CanTransport_TxPeek(transport, &frame) == 0U)
    {
        return;
    }

    transport->hw_tx_ok_count_seen = transport->hw.tx_ok_count;
    transport->hw_tx_error_count_seen = transport->hw.tx_error_count;

    if (CanHw_StartTx(&transport->hw, &frame) == 0U)
    {
        transport->tx_in_flight = 0U;
        transport->last_error = CAN_TRANSPORT_ERROR_HW_TX_FAIL;
        CanTransport_ClearFrame(&transport->current_tx_frame);
        CanTransport_TxDropFront(transport);
        return;
    }

    transport->current_tx_frame = frame;
    transport->tx_in_flight = 1U;
}

// transport와 하드웨어 바인딩을 기본 상태로 초기화한다.
static uint8_t CanTransport_Init(CanTransport *transport, uint8_t local_node_id)
{
    if (transport == NULL)
    {
        return 0U;
    }

    (void)memset(transport, 0, sizeof(*transport));
    if (CanHw_InitDefault(&transport->hw, local_node_id) == 0U)
    {
        return 0U;
    }

    transport->initialized = 1U;
    return 1U;
}

// transport 계층의 하드웨어 점검, RX 흡수, TX 진행을 한 번에 돌린다.
static void CanTransport_Task(CanTransport *transport, uint32_t now_ms)
{
    if ((transport == NULL) || (transport->initialized == 0U))
    {
        return;
    }

    CanHw_Task(&transport->hw, now_ms);
    CanTransport_DrainHwRx(transport);
    CanTransport_ProcessTx(transport);
}

// encode된 frame 하나를 transport TX queue에 적재한다.
static uint8_t CanTransport_SendFrame(CanTransport *transport, const CanFrame *frame)
{
    if ((transport == NULL) || (frame == NULL) || (transport->initialized == 0U))
    {
        if (transport != NULL)
        {
            transport->last_error = CAN_TRANSPORT_ERROR_NOT_READY;
        }
        return 0U;
    }

    if (CanTransport_TxIsFull(transport) != 0U)
    {
        transport->last_error = CAN_TRANSPORT_ERROR_TX_QUEUE_FULL;
        return 0U;
    }

    transport->tx_queue[transport->tx_tail] = *frame;
    transport->tx_tail = CanService_NextIndex(transport->tx_tail, CAN_TRANSPORT_TX_QUEUE_SIZE);
    transport->tx_count++;
    return 1U;
}

// transport RX queue에서 frame 하나를 꺼낸다.
static uint8_t CanTransport_PopRx(CanTransport *transport, CanFrame *out_frame)
{
    if ((transport == NULL) || (out_frame == NULL) || (transport->initialized == 0U))
    {
        return 0U;
    }

    if (transport->rx_count == 0U)
    {
        return 0U;
    }

    *out_frame = transport->rx_queue[transport->rx_head];
    CanTransport_ClearFrame(&transport->rx_queue[transport->rx_head]);
    transport->rx_head = CanService_NextIndex(transport->rx_head, CAN_TRANSPORT_RX_QUEUE_SIZE);
    transport->rx_count--;
    return 1U;
}

// decode된 수신 메시지를 app 전달용 incoming queue에 넣는다.
static uint8_t CanService_IncomingQueuePush(CanService *service, const CanMessage *message)
{
    if ((service == NULL) || (message == NULL))
    {
        return 0U;
    }

    if (service->incoming_count >= CAN_SERVICE_QUEUE_SIZE)
    {
        service->last_error = CAN_SERVICE_ERROR_RX_QUEUE_FULL;
        return 0U;
    }

    service->incoming_queue[service->incoming_tail] = *message;
    service->incoming_tail = CanService_NextIndex(service->incoming_tail, CAN_SERVICE_QUEUE_SIZE);
    service->incoming_count++;
    return 1U;
}

// 완료 결과를 result queue에 넣어 app 쪽이 추후 소비하게 한다.
static uint8_t CanService_ResultQueuePush(CanService *service, const CanServiceResult *result)
{
    if ((service == NULL) || (result == NULL))
    {
        return 0U;
    }

    if (service->result_count >= CAN_SERVICE_QUEUE_SIZE)
    {
        service->last_error = CAN_SERVICE_ERROR_RESULT_QUEUE_FULL;
        return 0U;
    }

    service->result_queue[service->result_tail] = *result;
    service->result_tail = CanService_NextIndex(service->result_tail, CAN_SERVICE_QUEUE_SIZE);
    service->result_count++;
    return 1U;
}

// 0을 피하면서 다음 request id를 하나 발급한다.
static uint8_t CanService_AllocateRequestId(CanService *service)
{
    uint8_t request_id;

    request_id = service->next_request_id++;
    if (service->next_request_id == 0U)
    {
        service->next_request_id = 1U;
    }

    if (request_id == 0U)
    {
        request_id = service->next_request_id++;
    }

    return request_id;
}

// 비어 있는 pending 슬롯을 찾는다.
static int32_t CanService_FindFreePendingSlot(CanService *service)
{
    uint8_t index;

    for (index = 0U; index < CAN_SERVICE_PENDING_SIZE; index++)
    {
        if (service->pending_table[index].in_use == 0U)
        {
            return (int32_t)index;
        }
    }

    return -1;
}

// 들어온 response와 짝이 맞는 pending request를 찾는다.
static int32_t CanService_FindPendingByResponse(CanService *service, const CanMessage *message)
{
    uint8_t index;

    for (index = 0U; index < CAN_SERVICE_PENDING_SIZE; index++)
    {
        if (service->pending_table[index].in_use == 0U)
        {
            continue;
        }

        if (service->pending_table[index].request_id != message->request_id)
        {
            continue;
        }

        if (service->pending_table[index].target_node_id != message->source_node_id)
        {
            continue;
        }

        return (int32_t)index;
    }

    return -1;
}

// 논리 메시지를 encode해 transport queue로 넘긴다.
static uint8_t CanService_SendMessage(CanService *service, const CanMessage *message)
{
    CanEncodedFrameList encoded_list;
    uint8_t             index;

    if ((service == NULL) || (message == NULL) || (service->initialized == 0U))
    {
        if (service != NULL)
        {
            service->last_error = CAN_SERVICE_ERROR_NOT_READY;
        }
        return 0U;
    }

    if (CanProto_EncodeMessage(&service->proto, message, &encoded_list) == 0U)
    {
        service->last_error = CAN_SERVICE_ERROR_PROTOCOL_ERROR;
        return 0U;
    }

    for (index = 0U; index < encoded_list.count; index++)
    {
        if (CanTransport_SendFrame(&service->transport, &encoded_list.frames[index]) == 0U)
        {
            service->last_error = CAN_SERVICE_ERROR_TX_QUEUE_FULL;
            return 0U;
        }
    }

    return 1U;
}

// pending request 정보를 timeout 결과 형태로 옮긴다.
static void CanService_FillTimeoutResult(const CanPendingRequest *pending,
                                         uint8_t local_node_id,
                                         CanServiceResult *out_result)
{
    CanService_ClearResult(out_result);
    out_result->kind = CAN_SERVICE_RESULT_TIMEOUT;
    out_result->request_id = pending->request_id;
    out_result->source_node_id = pending->target_node_id;
    out_result->target_node_id = local_node_id;
    out_result->command_code = pending->command_code;
    out_result->result_code = CAN_RES_TIMEOUT;
}

// 정상 response를 app이 읽기 쉬운 결과 구조로 옮긴다.
static void CanService_FillResponseResult(const CanPendingRequest *pending,
                                          const CanMessage *message,
                                          CanServiceResult *out_result)
{
    CanService_ClearResult(out_result);
    out_result->kind = CAN_SERVICE_RESULT_RESPONSE;
    out_result->request_id = message->request_id;
    out_result->source_node_id = message->source_node_id;
    out_result->target_node_id = message->target_node_id;
    out_result->command_code = pending->command_code;
    out_result->result_code = message->payload[0];
    out_result->detail_code = message->payload[1];
}

// response 하나를 pending request와 매칭해 완료 결과로 바꾼다.
static void CanService_ProcessResponse(CanService *service, const CanMessage *message)
{
    int32_t          slot_index;
    CanServiceResult result;

    slot_index = CanService_FindPendingByResponse(service, message);
    if (slot_index < 0)
    {
        return;
    }

    CanService_FillResponseResult(&service->pending_table[slot_index], message, &result);
    service->pending_table[slot_index].in_use = 0U;
    (void)CanService_ResultQueuePush(service, &result);
}

// decode된 메시지를 종류에 따라 response 처리나 incoming 전달로 나눈다.
static void CanService_ProcessDecodedMessage(CanService *service, const CanMessage *message)
{
    if (message->message_type == CAN_MSG_RESPONSE)
    {
        CanService_ProcessResponse(service, message);
        return;
    }

    if (CanService_IsAcceptedTarget(service, message->target_node_id) == 0U)
    {
        return;
    }

    (void)CanService_IncomingQueuePush(service, message);
}

// transport RX queue를 decode하며 상위 service queue에 옮긴다.
static void CanService_ProcessRx(CanService *service)
{
    CanFrame             frame;
    CanMessage           message;
    CanProtoDecodeStatus decode_status;

    while (CanTransport_PopRx(&service->transport, &frame) != 0U)
    {
        decode_status = CanProto_DecodeFrame(&service->proto, &frame, &message);
        if (decode_status == CAN_PROTO_DECODE_OK)
        {
            CanService_ProcessDecodedMessage(service, &message);
        }
        else if (decode_status == CAN_PROTO_DECODE_INVALID)
        {
            service->last_error = CAN_SERVICE_ERROR_PROTOCOL_ERROR;
        }
    }
}

// 오래 기다린 pending request를 timeout 결과로 정리한다.
static void CanService_ProcessTimeouts(CanService *service)
{
    uint8_t          index;
    CanServiceResult result;

    for (index = 0U; index < CAN_SERVICE_PENDING_SIZE; index++)
    {
        if (service->pending_table[index].in_use == 0U)
        {
            continue;
        }

        if (Infra_TimeIsExpired(service->current_tick_ms,
                                service->pending_table[index].start_tick_ms,
                                service->pending_table[index].timeout_ms) == 0U)
        {
            continue;
        }

        CanService_FillTimeoutResult(&service->pending_table[index],
                                     service->local_node_id,
                                     &result);
        service->pending_table[index].in_use = 0U;
        (void)CanService_ResultQueuePush(service, &result);
    }
}

// protocol과 transport, request 추적 상태를 초기화한다.
// service는 빈 queue와 새 request ID 흐름으로 시작하여,
// 상위 계층이 바로 작업을 제출할 수 있게 만든다.
uint8_t CanService_Init(CanService *service,
                        uint8_t local_node_id,
                        uint32_t default_timeout_ms)
{
    CanProtoConfig proto_config;
    uint8_t        index;

    if (service == NULL)
    {
        return 0U;
    }

    (void)memset(service, 0, sizeof(*service));
    service->local_node_id = local_node_id;
    service->default_timeout_ms = default_timeout_ms;
    service->next_request_id = 1U;

    for (index = 0U; index < CAN_SERVICE_PENDING_SIZE; index++)
    {
        CanService_ClearPending(&service->pending_table[index]);
    }

    proto_config.local_node_id = local_node_id;
    if (CanProto_Init(&service->proto, &proto_config) == 0U)
    {
        service->last_error = CAN_SERVICE_ERROR_PROTOCOL_ERROR;
        return 0U;
    }

    if (CanTransport_Init(&service->transport, local_node_id) == 0U)
    {
        service->last_error = CAN_SERVICE_ERROR_NOT_READY;
        return 0U;
    }

    service->initialized = 1U;
    return 1U;
}

// transport 작업을 진행시키고 새 RX 트래픽을 decode하며 timeout을 확인한다.
// 이 주기 task는 AppCore가 사용하는 CAN request/response service의,
// 중앙 실행 지점이다.
void CanService_Task(CanService *service, uint32_t now_ms)
{
    if ((service == NULL) || (service->initialized == 0U))
    {
        return;
    }

    service->current_tick_ms = now_ms;
    CanTransport_Task(&service->transport, now_ms);
    CanService_ProcessRx(service);
    CanService_ProcessTimeouts(service);
}

// 즉시 transport 한 번을 더 돌려 TX queue를 빨리 비우고 싶을 때 사용한다.
void CanService_FlushTx(CanService *service, uint32_t now_ms)
{
    if ((service == NULL) || (service->initialized == 0U))
    {
        return;
    }

    service->current_tick_ms = now_ms;
    CanTransport_Task(&service->transport, now_ms);
}

// 참고:
// 현재 transport와 service queue 정책은 단순해서 읽기 쉽지만,
// drop 통계나 재시도 기준을 더 세분화 필요, queue별 원인 기록을 조금 더 자세히 남겨야 함.

// CAN 전송용 논리 command 메시지 하나를 제출한다.
// 응답을 추적해야 하는 command는 먼저 pending slot을 확보한 뒤,
// encode된 메시지를 transport queue에 넣는다.
uint8_t CanService_SendCommand(CanService *service,
                               uint8_t target_node_id,
                               uint8_t command_code,
                               uint8_t arg0,
                               uint8_t arg1,
                               uint8_t need_response)
{
    CanMessage message;
    int32_t    slot_index;
    uint8_t    request_id;

    if ((service == NULL) || (service->initialized == 0U))
    {
        if (service != NULL)
        {
            service->last_error = CAN_SERVICE_ERROR_NOT_READY;
        }
        return 0U;
    }

    if (CanService_IsValidTarget(target_node_id) == 0U)
    {
        service->last_error = CAN_SERVICE_ERROR_INVALID_TARGET;
        return 0U;
    }

    (void)memset(&message, 0, sizeof(message));
    message.version = CAN_PROTO_VERSION_V1;
    message.message_type = CAN_MSG_COMMAND;
    message.source_node_id = service->local_node_id;
    message.target_node_id = target_node_id;
    message.payload_kind = CAN_PAYLOAD_CTRL_CMD;
    message.payload_length = 3U;
    message.payload[0] = command_code;
    message.payload[1] = arg0;
    message.payload[2] = arg1;

    request_id = 0U;
    slot_index = -1;
    if ((need_response != 0U) && (target_node_id != CAN_NODE_ID_BROADCAST))
    {
        slot_index = CanService_FindFreePendingSlot(service);
        if (slot_index < 0)
        {
            service->last_error = CAN_SERVICE_ERROR_PENDING_FULL;
            return 0U;
        }

        request_id = CanService_AllocateRequestId(service);
        message.request_id = request_id;
        message.flags |= CAN_MSG_FLAG_NEED_RESPONSE;
    }

    if (CanService_SendMessage(service, &message) == 0U)
    {
        return 0U;
    }

    if ((request_id != 0U) && (slot_index >= 0))
    {
        service->pending_table[slot_index].in_use = 1U;
        service->pending_table[slot_index].request_id = request_id;
        service->pending_table[slot_index].target_node_id = target_node_id;
        service->pending_table[slot_index].command_code = command_code;
        service->pending_table[slot_index].start_tick_ms = service->current_tick_ms;
        service->pending_table[slot_index].timeout_ms = service->default_timeout_ms;
    }

    return 1U;
}

// 이전에 추적하던 요청에 대한 protocol response를 보낸다.
// 호출자는 request ID와 result code만 제공하고,
// 메시지 조립과 전송은 service가 담당한다.
uint8_t CanService_SendResponse(CanService *service,
                                uint8_t target_node_id,
                                uint8_t request_id,
                                uint8_t result_code,
                                uint8_t detail_code)
{
    CanMessage message;

    if ((service == NULL) || (service->initialized == 0U))
    {
        return 0U;
    }

    if ((CanService_IsValidTarget(target_node_id) == 0U) || (request_id == 0U))
    {
        return 0U;
    }

    (void)memset(&message, 0, sizeof(message));
    message.version = CAN_PROTO_VERSION_V1;
    message.message_type = CAN_MSG_RESPONSE;
    message.source_node_id = service->local_node_id;
    message.target_node_id = target_node_id;
    message.request_id = request_id;
    message.payload_kind = CAN_PAYLOAD_CTRL_RESULT;
    message.payload_length = 3U;
    message.payload[0] = result_code;
    message.payload[1] = detail_code;
    return CanService_SendMessage(service, &message);
}

// 응답 추적이 없는 event 형태 CAN 메시지 하나를 보낸다.
// 이 경로는 pending slot 기반 request/response 흐름 대신,
// 비동기 알림을 보낼 때 사용한다.
uint8_t CanService_SendEvent(CanService *service,
                             uint8_t target_node_id,
                             uint8_t event_code,
                             uint8_t arg0,
                             uint8_t arg1)
{
    CanMessage message;

    if ((service == NULL) || (service->initialized == 0U))
    {
        return 0U;
    }

    if (CanService_IsValidTarget(target_node_id) == 0U)
    {
        return 0U;
    }

    (void)memset(&message, 0, sizeof(message));
    message.version = CAN_PROTO_VERSION_V1;
    message.message_type = CAN_MSG_EVENT;
    message.source_node_id = service->local_node_id;
    message.target_node_id = target_node_id;
    message.payload_kind = CAN_PAYLOAD_EVENT_DATA;
    message.payload_length = 3U;
    message.payload[0] = event_code;
    message.payload[1] = arg0;
    message.payload[2] = arg1;
    return CanService_SendMessage(service, &message);
}

// 짧은 printable text payload 하나를 CAN으로 보낸다.
// 검증은 여기서 수행하므로,
// 상위 계층은 target node와 보낼 메시지만 제공하면 된다.
uint8_t CanService_SendText(CanService *service,
                            uint8_t target_node_id,
                            uint8_t text_type,
                            const char *text)
{
    CanMessage message;
    size_t     text_length;

    if ((service == NULL) || (service->initialized == 0U))
    {
        if (service != NULL)
        {
            service->last_error = CAN_SERVICE_ERROR_NOT_READY;
        }
        return 0U;
    }

    if ((CanService_IsValidTarget(target_node_id) == 0U) || (CanService_IsPrintableAscii(text) == 0U))
    {
        service->last_error = CAN_SERVICE_ERROR_PROTOCOL_ERROR;
        return 0U;
    }

    text_length = strlen(text);
    if ((text_length == 0U) || (text_length > CAN_TEXT_MAX_LEN))
    {
        service->last_error = CAN_SERVICE_ERROR_PROTOCOL_ERROR;
        return 0U;
    }

    (void)memset(&message, 0, sizeof(message));
    message.version = CAN_PROTO_VERSION_V1;
    message.message_type = CAN_MSG_TEXT;
    message.source_node_id = service->local_node_id;
    message.target_node_id = target_node_id;
    message.payload_kind = CAN_PAYLOAD_TEXT_DATA;
    message.text_type = text_type;
    message.text_length = (uint8_t)text_length;
    (void)memcpy(message.text, text, text_length);
    message.text[text_length] = '\0';
    return CanService_SendMessage(service, &message);
}

// decode된 비-response 메시지 하나를 app 계층으로 꺼낸다.
// incoming command와 event, text는 모두,
// protocol 및 target 검증 뒤 이 queue를 통해 AppCore에 도달한다.
uint8_t CanService_PopReceivedMessage(CanService *service, CanMessage *out_message)
{
    if ((service == NULL) || (out_message == NULL) || (service->incoming_count == 0U))
    {
        return 0U;
    }

    *out_message = service->incoming_queue[service->incoming_head];
    CanService_ClearMessage(&service->incoming_queue[service->incoming_head]);
    service->incoming_head = CanService_NextIndex(service->incoming_head, CAN_SERVICE_QUEUE_SIZE);
    service->incoming_count--;
    return 1U;
}

// 완료된 request result 하나를 꺼내 operator 피드백에 사용한다.
// 이 결과는 응답과 매칭된 결과이거나,
// acknowledgement를 기대한 command의 timeout 합성 결과일 수 있다.
uint8_t CanService_PopResult(CanService *service, CanServiceResult *out_result)
{
    if ((service == NULL) || (out_result == NULL) || (service->result_count == 0U))
    {
        return 0U;
    }

    *out_result = service->result_queue[service->result_head];
    CanService_ClearResult(&service->result_queue[service->result_head]);
    service->result_head = CanService_NextIndex(service->result_head, CAN_SERVICE_QUEUE_SIZE);
    service->result_count--;
    return 1U;
}
