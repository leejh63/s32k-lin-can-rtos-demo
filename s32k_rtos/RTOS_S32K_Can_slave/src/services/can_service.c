// pending request를 관리하는 CAN service 구현 파일이다.
// request ID를 추적하고 수신 프레임을 해석하며,
// timeout 및 response 결과를 상위 계층에 제공한다.
#include "can_service_internal.h"

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
// transport와 결과 queue가 모두 같은 규칙을 쓰므로,
// wrap-around 처리를 한곳에 두면 가독성이 좋아진다.
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
// 브로드캐스트를 포함한 유효 범위만 통과시켜,
// 잘못된 주소가 protocol encode 단계까지 내려가지 않게 한다.
static uint8_t CanService_IsValidTarget(uint8_t node_id)
{
    if (node_id == CAN_NODE_ID_BROADCAST)
    {
        return 1U;
    }

    return ((node_id >= CAN_NODE_ID_MIN) && (node_id <= CAN_NODE_ID_MAX)) ? 1U : 0U;
}

// 수신 메시지가 현재 노드가 받아야 할 대상인지 본다.
// 자기 자신과 브로드캐스트만 통과시키면,
// 상위 app은 이미 검증된 입력만 처리하면 된다.
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

// text payload가 전송 가능한 짧은 ASCII 문장인지 확인한다.
// 이 service는 상태 문구 위주의 간단한 text만 다룬다는 전제라,
// 문자열 유효성도 여기서 함께 점검한다.
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

// transport 내부 frame 저장소를 빈 상태로 맞춘다.
// queue에서 빠진 자리나 현재 전송 슬롯을 정리할 때
// 오래된 데이터가 남아 보이지 않게 하려는 보조 함수다.
static void CanTransport_ClearFrame(CanFrame *frame)
{
    if (frame != NULL)
    {
        (void)memset(frame, 0, sizeof(*frame));
    }
}

// software TX queue가 가득 찼는지 빠르게 확인한다.
// 상위 전송 경로는 이 보조 함수로 여유를 먼저 보고,
// 꽉 찬 경우를 공통 오류 코드로 처리한다.
static uint8_t CanTransport_TxIsFull(const CanTransport *transport)
{
    return (transport->tx_count >= CAN_TRANSPORT_TX_QUEUE_SIZE) ? 1U : 0U;
}

// software RX queue가 가득 찼는지 확인한다.
// 수신 폭주 상황에서 drop 여부를 고를 때
// 현재 여유가 있는지만 짧게 판단하려는 용도다.
static uint8_t CanTransport_RxIsFull(const CanTransport *transport)
{
    return (transport->rx_count >= CAN_TRANSPORT_RX_QUEUE_SIZE) ? 1U : 0U;
}

// 다음에 보낼 frame을 queue에서 꺼내지 않고 먼저 본다.
// 실제 하드웨어 전송 시작이 성공한 뒤에만 front를 제거하려고,
// send 전 단계에서는 peek를 사용한다.
static uint8_t CanTransport_TxPeek(const CanTransport *transport, CanFrame *out_frame)
{
    if ((transport == NULL) || (out_frame == NULL) || (transport->tx_count == 0U))
    {
        return 0U;
    }

    *out_frame = transport->tx_queue[transport->tx_head];
    return 1U;
}

// TX queue 맨 앞 frame을 제거한다.
// 전송 완료나 전송 실패가 확정된 뒤에는
// 같은 frame을 다시 붙잡고 있을 이유가 없으므로 여기서 비워 둔다.
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

// 하드웨어에서 올라온 TX 결과를 transport 상태에 반영한다.
// 성공이면 front를 제거하고,
// 실패면 오류를 남긴 뒤 같은 frame을 정리하고 넘어간다.
static void CanTransport_OnTxResult(CanTransport *transport, CanHwTxResult tx_result)
{
    if (transport == NULL)
    {
        return;
    }

    transport->tx_in_flight = 0U;
    if (tx_result == CAN_HW_TX_RESULT_OK)
    {
        CanTransport_TxDropFront(transport);
        CanTransport_ClearFrame(&transport->current_tx_frame);
        return;
    }

    transport->last_error = CAN_TRANSPORT_ERROR_HW_TX_FAIL;
    CanTransport_TxDropFront(transport);
    CanTransport_ClearFrame(&transport->current_tx_frame);
}

// 하드웨어에서 건져 온 수신 frame 하나를 software RX queue에 넣는다.
// protocol decode는 상위 service에서 하도록 유지하고,
// 여기서는 frame 보관까지만 맡는다.
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

// 하드웨어 RX queue를 가능한 만큼 software queue로 옮긴다.
// transport는 frame을 잃지 않는 쪽에 먼저 집중하고,
// 해석은 뒤쪽 단계가 조금 늦게 따라오도록 흐름을 나눈다.
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

// 현재 전송 중인 frame을 마무리하거나 다음 frame을 시작한다.
// 이 함수 하나에서 진행 중 상태와 queue front를 함께 다루면,
// TX 흐름이 분산되지 않아 추적이 쉬워진다.
static void CanTransport_ProcessTx(CanTransport *transport)
{
    CanFrame      frame;
    CanHwTxResult tx_result;

    if (transport == NULL)
    {
        return;
    }

    if (transport->tx_in_flight != 0U)
    {
        // 1) 먼저 이전에 시작한 TX의 완료/실패를 정리한다.
        if (CanHw_TryTakeTxResult(&transport->hw, &tx_result) != 0U)
        {
            CanTransport_OnTxResult(transport, tx_result);
        }

        if (CanHw_IsTxBusy(&transport->hw) != 0U)
        {
            return;
        }

        if (CanHw_TryTakeTxResult(&transport->hw, &tx_result) != 0U)
        {
            CanTransport_OnTxResult(transport, tx_result);
        }

        if (transport->tx_in_flight != 0U)
        {
            CanTransport_OnTxResult(transport, CAN_HW_TX_RESULT_FAIL);
        }
    }

    // 2) 비어 있는 경우에만 queue front의 다음 frame을 시작한다.
    if (transport->tx_count == 0U)
    {
        return;
    }

    if (CanTransport_TxPeek(transport, &frame) == 0U)
    {
        return;
    }

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

// transport와 하드웨어 driver를 초기 상태로 준비한다.
// software queue를 먼저 비우고,
// 그다음 실제 CAN 하드웨어를 붙여 transport를 사용 가능한 상태로 만든다.
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

// transport 한 주기를 진행한다.
// 하드웨어 상태 갱신, RX 배수, TX 진행을 이 순서로 모아 두면
// 상위 service는 현재 tick에 transport를 한 번 돌렸다는 사실만 알면 된다.
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

// 새 frame 하나를 software TX queue 뒤에 붙인다.
// 바로 하드웨어에 보내지 않고 queue에 쌓아 두면,
// 상위 service가 encode 결과를 같은 방식으로 밀어 넣을 수 있다.
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

// software RX queue에서 frame 하나를 꺼낸다.
// decode 단계는 frame 단위로 천천히 소비하므로,
// transport 쪽에서는 안전하게 pop만 제공하면 충분하다.
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

// decode가 끝난 non-response 메시지를 incoming queue에 넣는다.
// app 계층은 command, event, text를 이 queue로 받으므로,
// 상위 공개 인터페이스를 위한 마지막 적재 지점이라고 보면 된다.
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

// request 결과를 result queue 뒤에 붙인다.
// timeout과 response를 같은 queue로 모아 두면,
// app은 한 가지 소비 경로만 따라가면 된다.
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

// 다음 request id를 하나 배정한다.
// 0은 미사용 의미로 비워 두고,
// 나머지 범위 안에서 순환하며 request-response 매칭에 쓴다.
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

// pending table에서 비어 있는 칸을 찾는다.
// 응답을 기다릴 요청 수를 고정 크기로 묶어 둔 만큼,
// 새 request는 먼저 빈 슬롯이 있는지 확인하고 들어간다.
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

// 들어온 response와 맞는 pending request를 찾는다.
// request id와 상대 노드가 모두 맞아야 같은 왕복으로 보고,
// 맞지 않으면 조용히 무시한다.
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

// 논리 메시지를 encode해서 transport queue에 밀어 넣는다.
// protocol 검증과 TX queue 적재를 한군데에서 처리하면,
// command/event/text 전송 함수들은 메시지 조립에 더 집중할 수 있다.
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

// timeout으로 끝난 pending request를 app용 결과 형태로 바꾼다.
// 실제 response가 오지 않았더라도,
// app은 이 결과 하나만 보고 동일한 방식으로 후속 처리를 할 수 있다.
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

// response 메시지를 app용 결과 구조로 변환한다.
// app은 원본 메시지 대신 이 결과만 받아도
// 어떤 command가 어떻게 끝났는지 충분히 알 수 있게 한다.
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

// 들어온 response를 pending table과 매칭해 결과 queue로 넘긴다.
// 같은 요청을 찾으면 그 자리의 추적은 종료하고,
// app이 읽을 요약 결과만 남긴다.
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

// decode가 끝난 메시지를 종류에 따라 다음 단계로 보낸다.
// response는 pending 매칭으로 보내고,
// 그 밖의 메시지는 target 검사를 거쳐 incoming queue에 넣는다.
// 주의: event/text도 여기까지는 들어오지만, 현재 app 정책이 command 위주라 상위에서 바로 활용되지 않을 수 있다.
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

// transport RX queue에 쌓인 frame을 모두 decode해 본다.
// 유효한 메시지만 app 경로로 넘기고,
// malformed frame은 protocol 오류로만 기록해 둔다.
static void CanService_ProcessRx(CanService *service)
{
    CanFrame             frame;
    CanMessage           message;
    CanProtoDecodeStatus decode_status;

    while (CanTransport_PopRx(&service->transport, &frame) != 0U)
    {
        // frame -> logical message decode -> message kind별 후속 처리 순서다.
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

// 아직 응답을 기다리는 요청들의 timeout을 확인한다.
// 기준 시각은 service가 현재 tick에 받은 now 값 하나로 통일해 두어,
// timeout 판정이 호출 순서에 흔들리지 않게 한다.
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

        // 응답을 끝내 못 받은 슬롯은 timeout 결과로 합성해 app에 넘긴다.
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

// 새로 queue에 넣은 TX frame이 있으면 즉시 한 번 더 transport를 돌린다.
// 다음 주기까지 꼭 기다리지 않아도 되는 상황이라,
// 전송 지연을 조금 줄이려는 가벼운 보조 함수다.
void CanService_FlushTx(CanService *service, uint32_t now_ms)
{
    if ((service == NULL) || (service->initialized == 0U))
    {
        return;
    }

    service->current_tick_ms = now_ms;
    CanTransport_Task(&service->transport, now_ms);
}

// CAN 전송용 논리 command 메시지 하나를 제출한다.
// 응답을 추적해야 하는 command는 먼저 pending slot을 확보한 뒤,
// encode된 메시지를 transport queue에 넣는다.
// 주의: pending slot은 send 성공 뒤에만 실제 사용 상태로 전환한다.
// 그래서 queue 적재 실패가 나도 반쯤 열린 pending은 남기지 않는다.
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
        // response가 필요한 unicast command만 pending table을 사용한다.
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

    // 실제 transport queue 적재가 성공한 뒤에만 pending 추적을 시작한다.
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

// 참고:
// 현재 transport와 service는 queue가 꽉 차면 비교적 보수적으로 실패를 남기고 다음 tick으로 넘기는 편이다.
// 현장 환경에서 혼잡이 더 심해질 가능성이 있으면 drop 통계, 재시도 정책, app까지의 실패 가시성을 더 또렷하게 유지하는 것이 적절하다.
