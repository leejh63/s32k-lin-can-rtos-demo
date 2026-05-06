// CAN driver 내부 저장소 정의다.
// service internal만 이 concrete layout을 알고,
// public driver header는 opaque handle만 유지한다.
#ifndef CAN_HW_INTERNAL_H
#define CAN_HW_INTERNAL_H

#include "can_hw.h"

struct CanHw
{
    uint8_t           initialized;
    uint8_t           local_node_id;
    uint8_t           instance;
    uint8_t           tx_mb_index;
    uint8_t           rx_mb_index;
    volatile uint8_t  tx_busy;
    volatile uint8_t  last_error;
    volatile uint8_t  tx_result_pending;
    volatile uint8_t  tx_result;
    CanFrame          rx_queue[CAN_HW_RX_QUEUE_SIZE];
    volatile uint8_t  rx_head;
    volatile uint8_t  rx_tail;
    volatile uint8_t  rx_count;
    volatile uint32_t tx_ok_count;
    volatile uint32_t tx_error_count;
    volatile uint32_t rx_ok_count;
    volatile uint32_t rx_error_count;
    volatile uint32_t rx_drop_count;
};

#endif
