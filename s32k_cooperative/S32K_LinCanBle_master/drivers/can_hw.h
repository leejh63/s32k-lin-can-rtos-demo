// 저수준 FlexCAN transport 인터페이스다.
// 상위 CAN 계층은 mailbox 설정과 generated driver 호출 대신,
// 작은 queue 중심 하드웨어 API만 사용하게 된다.
#ifndef CAN_HW_H
#define CAN_HW_H

#include "../services/can_types.h"

// FlexCAN mailbox 기반 raw CAN 하드웨어 context다.
// transport 계층은 이 상태를 사용해 수신 frame을 버퍼링하고,
// generated driver의 TX 완료 여부를 관찰한다.
typedef struct
{
    uint8_t           initialized;
    uint8_t           local_node_id;
    uint8_t           instance;
    uint8_t           tx_mb_index;
    uint8_t           rx_mb_index;
    volatile uint8_t  tx_busy;
    volatile uint8_t  last_error;
    CanFrame          rx_queue[CAN_HW_RX_QUEUE_SIZE];
    volatile uint8_t  rx_head;
    volatile uint8_t  rx_tail;
    volatile uint8_t  rx_count;
    volatile uint32_t tx_ok_count;
    volatile uint32_t tx_error_count;
    volatile uint32_t rx_ok_count;
    volatile uint32_t rx_error_count;
    volatile uint32_t rx_drop_count;
} CanHw;

uint8_t CanHw_InitDefault(CanHw *hw, uint8_t local_node_id);
void    CanHw_Task(CanHw *hw, uint32_t now_ms);
uint8_t CanHw_StartTx(CanHw *hw, const CanFrame *frame);
uint8_t CanHw_TryPopRx(CanHw *hw, CanFrame *out_frame);
uint8_t CanHw_IsReady(const CanHw *hw);
uint8_t CanHw_IsTxBusy(const CanHw *hw);
uint8_t CanHw_GetLastError(const CanHw *hw);

#endif
