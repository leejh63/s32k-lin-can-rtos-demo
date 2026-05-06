// 저수준 FlexCAN transport 인터페이스다.
// 상위 CAN 계층은 mailbox 설정과 generated driver 호출 대신,
// 작은 queue 중심 하드웨어 API만 사용하게 된다.
#ifndef CAN_HW_H
#define CAN_HW_H

#include "../core/can_types.h"

// concrete 저장소는 driver internal header에 숨기고,
// 상위에는 opaque handle과 작은 동작 계약만 노출한다.
typedef struct CanHw CanHw;

typedef enum
{
    CAN_HW_TX_RESULT_NONE = 0,
    CAN_HW_TX_RESULT_OK,
    CAN_HW_TX_RESULT_FAIL
} CanHwTxResult;

uint8_t CanHw_InitDefault(CanHw *hw, uint8_t local_node_id);
void    CanHw_Task(CanHw *hw, uint32_t now_ms);
uint8_t CanHw_StartTx(CanHw *hw, const CanFrame *frame);
uint8_t CanHw_TryPopRx(CanHw *hw, CanFrame *out_frame);
uint8_t CanHw_TryTakeTxResult(CanHw *hw, CanHwTxResult *out_result);
uint8_t CanHw_IsReady(const CanHw *hw);
uint8_t CanHw_IsTxBusy(const CanHw *hw);
uint8_t CanHw_GetLastError(const CanHw *hw);

#endif
