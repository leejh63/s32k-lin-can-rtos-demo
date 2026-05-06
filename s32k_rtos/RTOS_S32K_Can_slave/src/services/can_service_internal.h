// CAN service 내부 상태 선언이다.
// public header는 service 계약만 노출하고,
// transport / pending / queue 저장소는 이 내부 헤더에 가둔다.
#ifndef CAN_SERVICE_INTERNAL_H
#define CAN_SERVICE_INTERNAL_H

#include "can_service.h"

#include "../drivers/can_hw_internal.h"
#include "can_proto.h"

typedef struct
{
    uint8_t  in_use;
    uint8_t  request_id;
    uint8_t  target_node_id;
    uint8_t  command_code;
    uint32_t start_tick_ms;
    uint32_t timeout_ms;
} CanPendingRequest;

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
} CanTransport;

struct CanService
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
};

#endif
