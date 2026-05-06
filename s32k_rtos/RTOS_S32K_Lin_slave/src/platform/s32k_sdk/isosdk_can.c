// FlexCAN generated driver를 얇게 감싼 구현 파일이다.
// 이 프로젝트에서는 CAN을 직접 쓰지 않더라도,
// 공용 SDK 표면을 맞추기 위해 필요한 최소 API를 유지한다.
#include "isosdk_can.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "isosdk_sdk_bindings.h"

#ifdef ISOSDK_SDK_HAS_CAN

#define ISOSDK_CAN_FRAME_DATA_SIZE  16U

static flexcan_msgbuff_t s_iso_sdk_can_rx_msg;

// mailbox 설정과 전송에 공통으로 쓰는 data info 구조를 채운다.
static void IsoSdk_CanInitDataInfo(flexcan_data_info_t *data_info,
                                   uint8_t dlc,
                                   uint8_t is_extended_id,
                                   uint8_t is_remote_frame)
{
    if (data_info == NULL)
    {
        return;
    }

    (void)memset(data_info, 0, sizeof(*data_info));
    data_info->msg_id_type = (is_extended_id != 0U) ? FLEXCAN_MSG_ID_EXT : FLEXCAN_MSG_ID_STD;
    data_info->is_remote = (is_remote_frame != 0U) ? true : false;
    data_info->data_length = dlc;
    data_info->fd_enable = true;
    data_info->enable_brs = true;
    data_info->fd_padding = 0U;
}

// 현재 빌드에서 CAN 기능이 들어 있는지 알려준다.
uint8_t IsoSdk_CanIsSupported(void)
{
    return 1U;
}

// generated 설정이 가리키는 기본 CAN 인스턴스를 반환한다.
uint8_t IsoSdk_CanGetDefaultInstance(void)
{
    return ISOSDK_SDK_CAN_INSTANCE;
}

// controller state와 init config를 사용해 CAN controller를 초기화한다.
uint8_t IsoSdk_CanInitController(uint8_t instance)
{
    return (FLEXCAN_DRV_Init(instance,
                             &ISOSDK_SDK_CAN_STATE,
                             &ISOSDK_SDK_CAN_INIT_CONFIG) == STATUS_SUCCESS) ? 1U : 0U;
}

// 지정 mailbox를 기본 송신 mailbox로 설정한다.
uint8_t IsoSdk_CanInitTxMailbox(uint8_t instance, uint8_t tx_mb_index)
{
    flexcan_data_info_t data_info;

    IsoSdk_CanInitDataInfo(&data_info, ISOSDK_CAN_FRAME_DATA_SIZE, 0U, 0U);
    return (FLEXCAN_DRV_ConfigTxMb(instance, tx_mb_index, &data_info, 0U) == STATUS_SUCCESS) ? 1U : 0U;
}

// 지정 mailbox를 기본 수신 mailbox로 설정한다.
uint8_t IsoSdk_CanInitRxMailbox(uint8_t instance, uint8_t rx_mb_index)
{
    flexcan_data_info_t data_info;

    IsoSdk_CanInitDataInfo(&data_info, ISOSDK_CAN_FRAME_DATA_SIZE, 0U, 0U);
    return (FLEXCAN_DRV_ConfigRxMb(instance, rx_mb_index, &data_info, 0U) == STATUS_SUCCESS) ? 1U : 0U;
}

// RX mailbox mask를 열어 모든 표준 ID를 받게 한다.
uint8_t IsoSdk_CanConfigRxAcceptAll(uint8_t instance, uint8_t rx_mb_index)
{
    status_t status;

    FLEXCAN_DRV_SetRxMaskType(instance, FLEXCAN_RX_MASK_INDIVIDUAL);
    status = FLEXCAN_DRV_SetRxIndividualMask(instance,
                                             FLEXCAN_MSG_ID_STD,
                                             rx_mb_index,
                                             0x00000000U);
    return (status == STATUS_SUCCESS) ? 1U : 0U;
}

// 지정 RX mailbox에 다음 수신 버퍼를 다시 연결한다.
uint8_t IsoSdk_CanStartReceive(uint8_t instance, uint8_t rx_mb_index)
{
    (void)memset(&s_iso_sdk_can_rx_msg, 0, sizeof(s_iso_sdk_can_rx_msg));
    return (FLEXCAN_DRV_Receive(instance, rx_mb_index, &s_iso_sdk_can_rx_msg) == STATUS_SUCCESS) ? 1U : 0U;
}

// SDK 상태 코드를 상위에서 쓰는 전송 상태 enum으로 정리한다.
IsoSdkCanTransferState IsoSdk_CanGetTransferState(uint8_t instance, uint8_t mb_index)
{
    status_t status;

    status = FLEXCAN_DRV_GetTransferStatus(instance, mb_index);
    if (status == STATUS_SUCCESS)
    {
        return ISOSDK_CAN_TRANSFER_DONE;
    }

    if (status == STATUS_BUSY)
    {
        return ISOSDK_CAN_TRANSFER_BUSY;
    }

    return ISOSDK_CAN_TRANSFER_ERROR;
}

// 최근 수신 버퍼 내용을 공용 출력 인자로 꺼내 준다.
uint8_t IsoSdk_CanReadRxFrame(uint32_t now_ms,
                              uint32_t *out_id,
                              uint8_t *out_dlc,
                              uint8_t *out_is_extended_id,
                              uint8_t *out_is_remote_frame,
                              uint8_t *out_data,
                              uint8_t data_capacity)
{
    uint8_t dlc;

    if ((out_id == NULL) || (out_dlc == NULL) ||
        (out_is_extended_id == NULL) || (out_is_remote_frame == NULL) ||
        (out_data == NULL) || (data_capacity == 0U))
    {
        return 0U;
    }

    dlc = s_iso_sdk_can_rx_msg.dataLen;
    if (dlc > data_capacity)
    {
        dlc = data_capacity;
    }

    *out_id = s_iso_sdk_can_rx_msg.msgId;
    *out_dlc = dlc;
    *out_is_extended_id = 0U;
    *out_is_remote_frame = 0U;
    (void)now_ms;
    (void)memcpy(out_data, s_iso_sdk_can_rx_msg.data, dlc);
    return 1U;
}

// 공용 인자 형태로 받은 frame 하나를 SDK 전송 호출로 넘긴다.
uint8_t IsoSdk_CanSend(uint8_t instance,
                       uint8_t tx_mb_index,
                       uint32_t id,
                       uint8_t dlc,
                       const uint8_t *data,
                       uint8_t is_extended_id,
                       uint8_t is_remote_frame)
{
    flexcan_data_info_t data_info;

    if (data == NULL)
    {
        return 0U;
    }

    IsoSdk_CanInitDataInfo(&data_info, dlc, is_extended_id, is_remote_frame);
    return (FLEXCAN_DRV_Send(instance,
                             tx_mb_index,
                             &data_info,
                             id,
                             data) == STATUS_SUCCESS) ? 1U : 0U;
}

#else

// CAN이 빠진 빌드에서는 미지원 상태를 반환한다.
uint8_t IsoSdk_CanIsSupported(void)
{
    return 0U;
}

// 미지원 빌드용 기본값이다.
uint8_t IsoSdk_CanGetDefaultInstance(void)
{
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_CanInitController(uint8_t instance)
{
    (void)instance;
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_CanInitTxMailbox(uint8_t instance, uint8_t tx_mb_index)
{
    (void)instance;
    (void)tx_mb_index;
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_CanInitRxMailbox(uint8_t instance, uint8_t rx_mb_index)
{
    (void)instance;
    (void)rx_mb_index;
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_CanConfigRxAcceptAll(uint8_t instance, uint8_t rx_mb_index)
{
    (void)instance;
    (void)rx_mb_index;
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_CanStartReceive(uint8_t instance, uint8_t rx_mb_index)
{
    (void)instance;
    (void)rx_mb_index;
    return 0U;
}

// 미지원 빌드에서는 항상 error 상태로 본다.
IsoSdkCanTransferState IsoSdk_CanGetTransferState(uint8_t instance, uint8_t mb_index)
{
    (void)instance;
    (void)mb_index;
    return ISOSDK_CAN_TRANSFER_ERROR;
}

// 미지원 빌드에서는 frame을 읽을 수 없다.
uint8_t IsoSdk_CanReadRxFrame(uint32_t now_ms,
                              uint32_t *out_id,
                              uint8_t *out_dlc,
                              uint8_t *out_is_extended_id,
                              uint8_t *out_is_remote_frame,
                              uint8_t *out_data,
                              uint8_t data_capacity)
{
    (void)now_ms;
    (void)out_id;
    (void)out_dlc;
    (void)out_is_extended_id;
    (void)out_is_remote_frame;
    (void)out_data;
    (void)data_capacity;
    return 0U;
}

// 미지원 빌드에서는 전송 요청도 실패로 반환한다.
uint8_t IsoSdk_CanSend(uint8_t instance,
                       uint8_t tx_mb_index,
                       uint32_t id,
                       uint8_t dlc,
                       const uint8_t *data,
                       uint8_t is_extended_id,
                       uint8_t is_remote_frame)
{
    (void)instance;
    (void)tx_mb_index;
    (void)id;
    (void)dlc;
    (void)data;
    (void)is_extended_id;
    (void)is_remote_frame;
    return 0U;
}

#endif

// 참고:
// 이 프로젝트에서는 CAN이 핵심 경로가 아니어서 구현이 아주 얇지만,
// 추후 공용 코드에서 실제로 사용할 계획이 생기면 callback과 event 연결 규칙도 함께 맞춰 두는 것이 적절하다.
