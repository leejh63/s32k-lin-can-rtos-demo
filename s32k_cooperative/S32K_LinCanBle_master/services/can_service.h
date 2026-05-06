// CAN request/response service 인터페이스다.
// 이 계층은 raw transport와 protocol 모듈 위에,
// request 추적과 timeout 처리, 메시지 queue를 추가한다.
#ifndef CAN_SERVICE_H
#define CAN_SERVICE_H

#include "../core/infra_types.h"
#include "../drivers/can_hw.h"
#include "can_proto.h"

// 응답을 기다리는 추적 대상 송신 요청 하나를 나타낸다.
// service는 target과 command, timing 정보를 저장해 두고,
// 추후 response를 매칭하거나 timeout 결과를 만들어 낸다.
typedef struct
{
    uint8_t  in_use;
    uint8_t  request_id;
    uint8_t  target_node_id;
    uint8_t  command_code;
    uint32_t start_tick_ms;
    uint32_t timeout_ms;
} CanPendingRequest;

// raw 하드웨어 접근 위에 쌓인 CAN transport 상태다.
// 소프트웨어 TX/RX queue와,
// 현재 활성 하드웨어 전송에 대한 오류 상태 정리을 추가로 가진다.
// application 모듈이 사용하는 전체 CAN service context다.
// protocol과 transport, pending request, incoming message,
// result queue를 하나의 cohesive object로 묶어 관리한다.
typedef struct
{
    uint8_t initialized;
    uint8_t tx_in_flight;
    uint8_t last_error;
    CanHw   hw;
    CanFrame tx_queue[CAN_TRANSPORT_TX_QUEUE_SIZE];
    uint8_t  tx_head;
    uint8_t  tx_tail;
    uint8_t  tx_count;
    CanFrame rx_queue[CAN_TRANSPORT_RX_QUEUE_SIZE];
    uint8_t  rx_head;
    uint8_t  rx_tail;
    uint8_t  rx_count;
    CanFrame current_tx_frame;
    uint32_t hw_tx_ok_count_seen;
    uint32_t hw_tx_error_count_seen;
} CanTransport;

typedef struct
{
    uint8_t initialized;
    uint8_t local_node_id;
    uint8_t next_request_id;
    uint8_t last_error;
    uint32_t default_timeout_ms;
    uint32_t current_tick_ms;
    CanProto proto;
    CanTransport transport;
    CanPendingRequest pending_table[CAN_SERVICE_PENDING_SIZE];
    CanMessage incoming_queue[CAN_SERVICE_QUEUE_SIZE];
    uint8_t    incoming_head;
    uint8_t    incoming_tail;
    uint8_t    incoming_count;
    CanServiceResult result_queue[CAN_SERVICE_QUEUE_SIZE];
    uint8_t          result_head;
    uint8_t          result_tail;
    uint8_t          result_count;
} CanService;

uint8_t CanService_Init(CanService *service,
                        uint8_t local_node_id,
                        uint32_t default_timeout_ms);
void    CanService_Task(CanService *service, uint32_t now_ms);
void    CanService_FlushTx(CanService *service, uint32_t now_ms);
uint8_t CanService_SendCommand(CanService *service,
                               uint8_t target_node_id,
                               uint8_t command_code,
                               uint8_t arg0,
                               uint8_t arg1,
                               uint8_t need_response);
uint8_t CanService_SendResponse(CanService *service,
                                uint8_t target_node_id,
                                uint8_t request_id,
                                uint8_t result_code,
                                uint8_t detail_code);
uint8_t CanService_SendEvent(CanService *service,
                             uint8_t target_node_id,
                             uint8_t event_code,
                             uint8_t arg0,
                             uint8_t arg1);
uint8_t CanService_SendText(CanService *service,
                            uint8_t target_node_id,
                            uint8_t text_type,
                            const char *text);
uint8_t CanService_PopReceivedMessage(CanService *service, CanMessage *out_message);
uint8_t CanService_PopResult(CanService *service, CanServiceResult *out_result);

#endif
