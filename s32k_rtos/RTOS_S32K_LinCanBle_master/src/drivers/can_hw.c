// FlexCAN mailbox 처리와 raw frame 버퍼링을 구현한 파일이다.
// IsoSdk 계층이 generated driver 호출을 담당하고,
// 상위 계층에는 단순한 push/pop 형태의 transport 인터페이스만 노출한다.
#include "can_hw.h"

#include <stddef.h>
#include <string.h>

#include "interrupt_manager.h"

#include "../platform/s32k_sdk/isosdk_can.h"

// NOTE:
// 현재 CAN HW 계층은 단일 인스턴스 전제를 둔다.
// 프로젝트 규모에는 단순해서 좋지만, 다채널/다인스턴스로 확장하려면
// callback context 전달 구조를 더 일반화해야 한다.
static CanHw *s_can_hw_instance;

// raw CAN frame 저장 공간을 초기화한다.
// queue에서 제거한 뒤 상태를 정리하거나,
// 새 frame을 채우기 전에 기본값을 맞출 때 사용한다.
static void CanHw_ClearFrame(CanFrame *frame)
{
    if (frame != NULL)
    {
        (void)memset(frame, 0, sizeof(*frame));
    }
}

// 고정 크기 ring buffer의 다음 위치를 계산한다.
// 하드웨어 수신 queue 전용 보조 함수로 분리하여,
// callback 경로의 인덱스 갱신을 짧고 일정하게 유지한다.
static uint8_t CanHw_NextIndex(uint8_t index, uint8_t capacity)
{
    index++;
    if (index >= capacity)
    {
        index = 0U;
    }

    return index;
}

// callback에서 받은 raw frame을 소프트웨어 RX queue에 적재한다.
// decode 이전 단계의 데이터를 임시 보관해 두고,
// 상위 task가 안전한 문맥에서 순차적으로 소비할 수 있게 한다.
static uint8_t CanHw_RxQueuePush(CanHw *hw, const CanFrame *frame)
{
    if ((hw == NULL) || (frame == NULL))
    {
        return 0U;
    }

    if (hw->rx_count >= CAN_HW_RX_QUEUE_SIZE)
    {
        return 0U;
    }

    hw->rx_queue[hw->rx_tail] = *frame;
    hw->rx_tail = CanHw_NextIndex(hw->rx_tail, CAN_HW_RX_QUEUE_SIZE);
    hw->rx_count++;
    return 1U;
}

// IsoSdk가 보관한 최근 RX 결과를 공용 CanFrame 형태로 복사한다.
// 상위 계층이 generated driver 구조체를 직접 알 필요가 없도록,
// 이 단계에서 필요한 필드만 추린다.
static uint8_t CanHw_CopyIsoSdkRxToFrame(uint32_t now_ms, CanFrame *out_frame)
{
    uint8_t dlc;

    if (out_frame == NULL)
    {
        return 0U;
    }

    CanHw_ClearFrame(out_frame);
    dlc = 0U;
    if (IsoSdk_CanReadRxFrame(now_ms,
                              &out_frame->id,
                              &dlc,
                              &out_frame->is_extended_id,
                              &out_frame->is_remote_frame,
                              out_frame->data,
                              CAN_FRAME_DATA_SIZE) == 0U)
    {
        return 0U;
    }

    out_frame->dlc = dlc;
    out_frame->timestamp_ms = now_ms;
    return 1U;
}

// 다음 수신을 받을 수 있도록 mailbox를 다시 준비한다.
// 수신 재무장 로직이 여러 경로에서 반복되므로,
// 짧은 보조 함수로 묶어 에러 처리 규칙을 일관되게 유지한다.
static uint8_t CanHw_StartReceive(CanHw *hw)
{
    if (hw == NULL)
    {
        return 0U;
    }

    return IsoSdk_CanStartReceive(hw->instance, hw->rx_mb_index);
}

// RX 완료 callback에서 frame 하나를 큐에 적재하고 다음 수신을 준비한다.
// 이 경로에서는 raw data 적재와 재시작만 수행하며,
// decode와 protocol 판단은 상위 task 문맥에서 처리한다.
static void CanHw_OnRxComplete(CanHw *hw)
{
    CanFrame frame;

    if ((hw == NULL) || (hw->initialized == 0U))
    {
        return;
    }

    if (CanHw_CopyIsoSdkRxToFrame(0U, &frame) != 0U)
    {
        if (CanHw_RxQueuePush(hw, &frame) != 0U)
        {
            hw->rx_ok_count++;
        }
        else
        {
            hw->rx_drop_count++;
            hw->last_error = CAN_HW_ERROR_RX_QUEUE_FULL;
        }
    }
    else
    {
        hw->rx_error_count++;
        hw->last_error = CAN_HW_ERROR_RX_STATUS_FAIL;
    }

    if (CanHw_StartReceive(hw) == 0U)
    {
        hw->rx_error_count++;
        hw->last_error = CAN_HW_ERROR_RX_RESTART_FAIL;
    }
}

// TX 완료 사실을 기록하고 busy 상태를 해제한다.
// 상위 계층은 별도 완료 슬롯 대신,
// 카운터 변화와 busy flag를 기준으로 후속 정리를 수행한다.
static void CanHw_OnTxComplete(CanHw *hw)
{
    if ((hw == NULL) || (hw->initialized == 0U))
    {
        return;
    }

    hw->tx_busy = 0U;
    hw->tx_ok_count++;
}

// IsoSdk callback 서명을 CanHw 내부 처리 흐름에 연결한다.
// RX와 TX mailbox를 한곳에서 구분하여,
// 상위 계층에 SDK 이벤트 세부가 노출되지 않게 한다.
static void CanHw_OnIsoSdkEvent(void *context, uint8_t event_id, uint8_t mb_index)
{
    CanHw *hw;

    hw = (context != NULL) ? (CanHw *)context : s_can_hw_instance;
    if ((hw == NULL) || (hw->initialized == 0U))
    {
        return;
    }

    if ((event_id == ISOSDK_CAN_EVENT_RX_DONE) && (mb_index == hw->rx_mb_index))
    {
        CanHw_OnRxComplete(hw);
        return;
    }

    if ((event_id == ISOSDK_CAN_EVENT_TX_DONE) && (mb_index == hw->tx_mb_index))
    {
        CanHw_OnTxComplete(hw);
        return;
    }

    if (event_id == ISOSDK_CAN_EVENT_ERROR)
    {
        hw->last_error = CAN_HW_ERROR_RX_STATUS_FAIL;
    }
}

// 기본 CAN controller와 RX/TX mailbox 구성을 초기화한다.
// 이 함수가 끝나면 수신 재무장까지 완료되므로,
// 상위 service는 이후 송수신 task만 호출하면 된다.
uint8_t CanHw_InitDefault(CanHw *hw, uint8_t local_node_id)
{
    if (hw == NULL)
    {
        return 0U;
    }

    (void)memset(hw, 0, sizeof(*hw));
    hw->local_node_id = local_node_id;
    hw->tx_mb_index = CAN_HW_TX_MB_INDEX;
    hw->rx_mb_index = CAN_HW_RX_MB_INDEX;
    hw->last_error = CAN_HW_ERROR_NONE;
    s_can_hw_instance = hw;

    if (IsoSdk_CanIsSupported() == 0U)
    {
        hw->last_error = CAN_HW_ERROR_INIT_FAIL;
        return 0U;
    }

    hw->instance = IsoSdk_CanGetDefaultInstance();
    if (IsoSdk_CanInitController(hw->instance) == 0U)
    {
        hw->last_error = CAN_HW_ERROR_INIT_FAIL;
        return 0U;
    }

    IsoSdk_CanInstallEventCallback(CanHw_OnIsoSdkEvent, hw);

    if (IsoSdk_CanInitTxMailbox(hw->instance, hw->tx_mb_index) == 0U)
    {
        hw->last_error = CAN_HW_ERROR_TX_CONFIG_FAIL;
        return 0U;
    }

    if (IsoSdk_CanConfigRxAcceptAll(hw->instance, hw->rx_mb_index) == 0U)
    {
        hw->last_error = CAN_HW_ERROR_RX_CONFIG_FAIL;
        return 0U;
    }

    if (IsoSdk_CanInitRxMailbox(hw->instance, hw->rx_mb_index) == 0U)
    {
        hw->last_error = CAN_HW_ERROR_RX_CONFIG_FAIL;
        return 0U;
    }

    hw->initialized = 1U;
    if (CanHw_StartReceive(hw) == 0U)
    {
        hw->initialized = 0U;
        hw->last_error = CAN_HW_ERROR_RX_RESTART_FAIL;
        return 0U;
    }

    return 1U;
}

// callback 밖에서 전송 상태와 수신 복구 상태를 점검한다.
// IRQ에서 처리하지 않은 에러 감시를 이 함수가 담당하여,
// 하드웨어 상태가 어긋났을 때 수신을 다시 준비할 수 있게 한다.
void CanHw_Task(CanHw *hw, uint32_t now_ms)
{
    IsoSdkCanTransferState transfer_state;

    (void)now_ms;

    if ((hw == NULL) || (hw->initialized == 0U))
    {
        return;
    }

    if (hw->tx_busy != 0U)
    {
        transfer_state = IsoSdk_CanGetTransferState(hw->instance, hw->tx_mb_index);
        if (transfer_state == ISOSDK_CAN_TRANSFER_ERROR)
        {
            hw->tx_busy = 0U;
            hw->tx_error_count++;
            hw->last_error = CAN_HW_ERROR_TX_STATUS_FAIL;
        }
    }

    transfer_state = IsoSdk_CanGetTransferState(hw->instance, hw->rx_mb_index);
    if (transfer_state == ISOSDK_CAN_TRANSFER_ERROR)
    {
        hw->rx_error_count++;
        hw->last_error = CAN_HW_ERROR_RX_STATUS_FAIL;
        if (CanHw_StartReceive(hw) == 0U)
        {
            hw->last_error = CAN_HW_ERROR_RX_RESTART_FAIL;
        }
    }
}

// raw frame 하나를 실제 mailbox 전송으로 보낸다.
// 준비 상태와 busy 여부를 먼저 확인하고,
// 성공했을 때만 전송 중 상태로 전환한다.
uint8_t CanHw_StartTx(CanHw *hw, const CanFrame *frame)
{
    uint8_t dlc;

    if ((hw == NULL) || (frame == NULL) || (hw->initialized == 0U))
    {
        if (hw != NULL)
        {
            hw->last_error = CAN_HW_ERROR_NOT_READY;
        }
        return 0U;
    }

    if (hw->tx_busy != 0U)
    {
        hw->last_error = CAN_HW_ERROR_TX_START_FAIL;
        return 0U;
    }

    dlc = frame->dlc;
    if (dlc > CAN_FRAME_DATA_SIZE)
    {
        dlc = CAN_FRAME_DATA_SIZE;
    }

    if (IsoSdk_CanSend(hw->instance,
                       hw->tx_mb_index,
                       frame->id,
                       dlc,
                       frame->data,
                       frame->is_extended_id,
                       frame->is_remote_frame) == 0U)
    {
        hw->tx_error_count++;
        hw->last_error = CAN_HW_ERROR_TX_START_FAIL;
        return 0U;
    }

    hw->tx_busy = 1U;
    return 1U;
}

// callback이 적재해 둔 RX frame 하나를 task 문맥으로 꺼낸다.
// queue head 조정은 IRQ와 겹칠 수 있으므로,
// 짧게 보호 구간을 잡고 일관된 값만 넘긴다.
uint8_t CanHw_TryPopRx(CanHw *hw, CanFrame *out_frame)
{
    if ((hw == NULL) || (out_frame == NULL) || (hw->initialized == 0U))
    {
        return 0U;
    }

    INT_SYS_DisableIRQGlobal();
    if (hw->rx_count == 0U)
    {
        INT_SYS_EnableIRQGlobal();
        return 0U;
    }

    *out_frame = hw->rx_queue[hw->rx_head];
    CanHw_ClearFrame(&hw->rx_queue[hw->rx_head]);
    hw->rx_head = CanHw_NextIndex(hw->rx_head, CAN_HW_RX_QUEUE_SIZE);
    hw->rx_count--;
    INT_SYS_EnableIRQGlobal();
    return 1U;
}

// 초기화가 끝난 하드웨어 인스턴스인지 간단히 확인한다.
uint8_t CanHw_IsReady(const CanHw *hw)
{
    return (hw != NULL) ? hw->initialized : 0U;
}

// 현재 mailbox 전송이 아직 진행 중인지 알려준다.
uint8_t CanHw_IsTxBusy(const CanHw *hw)
{
    return (hw != NULL) ? hw->tx_busy : 0U;
}

// 가장 최근에 기록된 하드웨어 계층 에러 코드를 반환한다.
uint8_t CanHw_GetLastError(const CanHw *hw)
{
    if (hw == NULL)
    {
        return CAN_HW_ERROR_NOT_READY;
    }

    return hw->last_error;
}

// 참고:
// RX 완료 callback이 frame 복사와 queue 적재, 재수신 시작까지 맡고 있어서 현재 규모에는 충분하지만,
// 버스 부하가 더 커지면 callback 안에서 하는 일을 더 짧게 나누는 쪽이 안전하다.
