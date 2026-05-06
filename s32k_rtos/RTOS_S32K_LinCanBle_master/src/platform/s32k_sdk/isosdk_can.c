// FlexCAN generated driver를 조금 더 다루기 쉬운 표면으로 감싼 구현 파일이다.
// 상위 계층은 mailbox 설정, callback 타입, SDK 상태 코드를 직접 알 필요 없이,
// 이 파일이 제공하는 공용 형태만 사용하면 된다.
#include "isosdk_can.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "isosdk_sdk_bindings.h"

#ifdef ISOSDK_SDK_HAS_CAN

#define ISOSDK_CAN_FRAME_DATA_SIZE  16U
#define ISOSDK_CAN_INVALID_MB_INDEX 0xFFU

static flexcan_msgbuff_t      s_iso_sdk_can_rx_msg;
static IsoSdkCanEventCallback s_iso_sdk_can_event_cb;
static void                  *s_iso_sdk_can_event_context;

// 등록된 상위 callback으로 SDK 이벤트를 그대로 전달한다.
// 호출 지점을 따로 두면,
// callback 유무 확인과 공통 전달 규칙을 한곳에서 유지할 수 있다.
static void IsoSdk_CanDispatchEvent(uint8_t event_id, uint8_t mb_index)
{
    if (s_iso_sdk_can_event_cb == NULL)
    {
        return;
    }

    s_iso_sdk_can_event_cb(s_iso_sdk_can_event_context, event_id, mb_index);
}

// FlexCAN driver callback을 IsoSdk 공용 이벤트 값으로 번역한다.
// 상위 계층은 SDK enum 전체를 알 필요가 없고,
// 실제로 필요한 RX/TX 완료만 더 단순한 형태로 받게 된다.
static void IsoSdk_CanSdkEventCallback(uint8_t instance,
                                       flexcan_event_type_t eventType,
                                       uint32_t buffIdx,
                                       flexcan_state_t *flexcanState)
{
    uint8_t event_id;

    (void)instance;
    (void)flexcanState;

    event_id = ISOSDK_CAN_EVENT_NONE;
    switch (eventType)
    {
        case FLEXCAN_EVENT_RX_COMPLETE:
            event_id = ISOSDK_CAN_EVENT_RX_DONE;
            break;

        case FLEXCAN_EVENT_TX_COMPLETE:
            event_id = ISOSDK_CAN_EVENT_TX_DONE;
            break;

        default:
            break;
    }

    if (event_id != ISOSDK_CAN_EVENT_NONE)
    {
        IsoSdk_CanDispatchEvent(event_id, (uint8_t)buffIdx);
    }
}

// SDK 에러 callback을 공통 error 이벤트 하나로 모은다.
static void IsoSdk_CanSdkErrorCallback(uint8_t instance,
                                       flexcan_event_type_t eventType,
                                       flexcan_state_t *flexcanState)
{
    (void)instance;
    (void)eventType;
    (void)flexcanState;
    IsoSdk_CanDispatchEvent(ISOSDK_CAN_EVENT_ERROR, ISOSDK_CAN_INVALID_MB_INDEX);
}

// 전송과 mailbox 설정에 공통으로 쓰는 data info 구조를 채운다.
// FD 사용, ID 형식, remote frame 여부처럼 반복되는 설정을
// 여기서 한 번에 맞춰 두면 나머지 함수가 훨씬 짧아진다.
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

// 상위 계층 callback을 등록하고 SDK callback과 연결한다.
// context를 함께 보관해 두어,
// 이후 모든 이벤트가 같은 상위 객체로 돌아가게 만든다.
void IsoSdk_CanInstallEventCallback(IsoSdkCanEventCallback event_cb,
                                    void *event_context)
{
    s_iso_sdk_can_event_cb = event_cb;
    s_iso_sdk_can_event_context = event_context;

    FLEXCAN_DRV_InstallEventCallback(ISOSDK_SDK_CAN_INSTANCE,
                                     (event_cb != NULL) ? IsoSdk_CanSdkEventCallback : NULL,
                                     NULL);
    FLEXCAN_DRV_InstallErrorCallback(ISOSDK_SDK_CAN_INSTANCE,
                                     (event_cb != NULL) ? IsoSdk_CanSdkErrorCallback : NULL,
                                     NULL);
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
// 현재 프로젝트는 상위 protocol에서 target을 걸러내므로,
// 하드웨어 단계에서는 먼저 넓게 받는 쪽을 택하고 있다.
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
// 지금 프로젝트에서는 timestamp를 쓰지 않지만,
// 서명은 유지해 두어 상위 계층 인터페이스와 흐름을 맞춘다.
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

// 미지원 빌드에서는 callback 등록만 무시한다.
void IsoSdk_CanInstallEventCallback(IsoSdkCanEventCallback event_cb,
                                    void *event_context)
{
    (void)event_cb;
    (void)event_context;
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
// 수신 버퍼를 단일 정적 객체로 들고 있어 현재 구조에는 충분하지만,
// mailbox를 여러 개 병행하거나 burst traffic을 더 직접 다루게 되면 buffer 전략을 다시 분리하는 것이 적절하다.
