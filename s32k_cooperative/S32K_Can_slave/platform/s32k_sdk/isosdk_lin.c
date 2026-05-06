// LIN generated driver를 상위 계층이 다루기 쉬운 형태로 감싼 구현 파일이다.
// event callback, timeout service, RX/TX 시작 흐름을 이 파일에 모아 두어,
// 서비스 계층이 SDK 구조체에 직접 기대지 않게 만든다.
#include "isosdk_lin.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "isosdk_sdk_bindings.h"

#ifdef ISOSDK_SDK_HAS_LIN

static IsoSdkLinContext *s_iso_sdk_lin_context;

// 등록된 상위 callback으로 LIN 이벤트를 전달한다.
static void IsoSdk_LinDispatchEvent(uint8_t event_id, uint8_t current_pid)
{
    if ((s_iso_sdk_lin_context == NULL) ||
        (s_iso_sdk_lin_context->event_cb == NULL))
    {
        return;
    }

    s_iso_sdk_lin_context->event_cb(s_iso_sdk_lin_context->event_context,
                                    event_id,
                                    current_pid);
}

// SDK callback에서 현재 LIN 상태를 읽고 공용 이벤트 값으로 번역한다.
// timeout, PID 인식, RX/TX 완료를 여기서 정리해 두면,
// 상위 계층은 SDK 이벤트 종류를 개별적으로 알 필요가 없다.
static void IsoSdk_LinSdkCallback(uint32_t instance, void *lin_state_ptr)
{
    lin_state_t *lin_state;
    uint8_t      event_id;
    uint8_t      current_pid;

    (void)instance;

    lin_state = (lin_state_t *)lin_state_ptr;
    if (lin_state == NULL)
    {
        return;
    }

    current_pid = lin_state->currentId;
    if (lin_state->timeoutCounterFlag != false)
    {
        lin_state->timeoutCounterFlag = false;
        IsoSdk_LinDispatchEvent(ISOSDK_LIN_EVENT_ERROR, current_pid);
        return;
    }

    if (lin_state->currentEventId == LIN_RECV_BREAK_FIELD_OK)
    {
        if ((s_iso_sdk_lin_context != NULL) && (s_iso_sdk_lin_context->initialized != 0U))
        {
            LIN_DRV_SetTimeoutCounter(ISOSDK_SDK_LIN_INSTANCE,
                                      s_iso_sdk_lin_context->timeout_ticks);
        }
        return;
    }

    event_id = ISOSDK_LIN_EVENT_NONE;
    switch (lin_state->currentEventId)
    {
        case LIN_PID_OK:
            event_id = ISOSDK_LIN_EVENT_PID_OK;
            break;

        case LIN_RX_COMPLETED:
            event_id = ISOSDK_LIN_EVENT_RX_DONE;
            break;

        case LIN_TX_COMPLETED:
            event_id = ISOSDK_LIN_EVENT_TX_DONE;
            break;

        case LIN_PID_ERROR:
        case LIN_CHECKSUM_ERROR:
        case LIN_READBACK_ERROR:
        case LIN_FRAME_ERROR:
        case LIN_RX_OVERRUN:
        case LIN_SYNC_ERROR:
            event_id = ISOSDK_LIN_EVENT_ERROR;
            break;

        default:
            break;
    }

    if (event_id != ISOSDK_LIN_EVENT_NONE)
    {
        IsoSdk_LinDispatchEvent(event_id, current_pid);
    }
}

// 현재 빌드에서 LIN을 사용할 수 있는지 알려준다.
uint8_t IsoSdk_LinIsSupported(void)
{
    return 1U;
}

// LIN 노드를 master/slave 역할에 맞춰 초기화하고 callback을 연결한다.
// timeout tick 값도 함께 저장해 두어,
// break 이후 timeout counter를 다시 걸 때 같은 기준을 재사용한다.
uint8_t IsoSdk_LinInit(IsoSdkLinContext *context,
                       uint8_t role,
                       uint16_t timeout_ticks,
                       IsoSdkLinEventCallback event_cb,
                       void *event_context)
{
    status_t status;

    if (context == NULL)
    {
        return 0U;
    }

    (void)memset(context, 0, sizeof(*context));
    context->timeout_ticks = timeout_ticks;
    context->role = role;
    context->event_cb = event_cb;
    context->event_context = event_context;

    ISOSDK_SDK_LIN_INIT_CONFIG.nodeFunction =
        (role == ISOSDK_LIN_ROLE_MASTER) ? MASTER : SLAVE;
    ISOSDK_SDK_LIN_INIT_CONFIG.autobaudEnable = false;

    status = LIN_DRV_Init(ISOSDK_SDK_LIN_INSTANCE,
                          &ISOSDK_SDK_LIN_INIT_CONFIG,
                          &ISOSDK_SDK_LIN_STATE);
    if (status != STATUS_SUCCESS)
    {
        return 0U;
    }

    s_iso_sdk_lin_context = context;
    (void)LIN_DRV_InstallCallback(ISOSDK_SDK_LIN_INSTANCE, IsoSdk_LinSdkCallback);
    context->initialized = 1U;
    return 1U;
}

// master 역할에서 header 하나를 송신한다.
uint8_t IsoSdk_LinMasterSendHeader(IsoSdkLinContext *context, uint8_t pid)
{
    if ((context == NULL) || (context->initialized == 0U))
    {
        return 0U;
    }

    return (LIN_DRV_MasterSendHeader(ISOSDK_SDK_LIN_INSTANCE, pid) == STATUS_SUCCESS) ? 1U : 0U;
}

// 다음 LIN data field를 받을 버퍼를 driver에 연결한다.
uint8_t IsoSdk_LinStartReceive(IsoSdkLinContext *context, uint8_t *buffer, uint8_t length)
{
    if ((context == NULL) || (context->initialized == 0U) ||
        (buffer == NULL) || (length == 0U))
    {
        return 0U;
    }

    return (LIN_DRV_ReceiveFrameData(ISOSDK_SDK_LIN_INSTANCE, buffer, length) == STATUS_SUCCESS) ? 1U : 0U;
}

// 준비된 payload를 data field 송신으로 시작한다.
uint8_t IsoSdk_LinStartSend(IsoSdkLinContext *context, const uint8_t *buffer, uint8_t length)
{
    if ((context == NULL) || (context->initialized == 0U) ||
        (buffer == NULL) || (length == 0U))
    {
        return 0U;
    }

    return (LIN_DRV_SendFrameData(ISOSDK_SDK_LIN_INSTANCE, buffer, length) == STATUS_SUCCESS) ? 1U : 0U;
}

// 현재 LIN state machine을 idle로 되돌린다.
void IsoSdk_LinGotoIdle(IsoSdkLinContext *context)
{
    if ((context == NULL) || (context->initialized == 0U))
    {
        return;
    }

    (void)LIN_DRV_GotoIdleState(ISOSDK_SDK_LIN_INSTANCE);
}

// timeout 기준을 바꾸고 driver counter에도 같은 값을 반영한다.
void IsoSdk_LinSetTimeout(IsoSdkLinContext *context, uint16_t timeout_ticks)
{
    if ((context == NULL) || (context->initialized == 0U))
    {
        return;
    }

    context->timeout_ticks = timeout_ticks;
    LIN_DRV_SetTimeoutCounter(ISOSDK_SDK_LIN_INSTANCE, timeout_ticks);
}

// timer ISR이나 짧은 tick 문맥에서 timeout service를 한 번 진행한다.
void IsoSdk_LinServiceTick(IsoSdkLinContext *context)
{
    if ((context == NULL) || (context->initialized == 0U))
    {
        return;
    }

    LIN_DRV_TimeoutService(ISOSDK_SDK_LIN_INSTANCE);
}

#else

// LIN이 빠진 빌드에서는 미지원 상태를 반환한다.
uint8_t IsoSdk_LinIsSupported(void)
{
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_LinInit(IsoSdkLinContext *context,
                       uint8_t role,
                       uint16_t timeout_ticks,
                       IsoSdkLinEventCallback event_cb,
                       void *event_context)
{
    (void)context;
    (void)role;
    (void)timeout_ticks;
    (void)event_cb;
    (void)event_context;
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_LinMasterSendHeader(IsoSdkLinContext *context, uint8_t pid)
{
    (void)context;
    (void)pid;
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_LinStartReceive(IsoSdkLinContext *context, uint8_t *buffer, uint8_t length)
{
    (void)context;
    (void)buffer;
    (void)length;
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_LinStartSend(IsoSdkLinContext *context, const uint8_t *buffer, uint8_t length)
{
    (void)context;
    (void)buffer;
    (void)length;
    return 0U;
}

// 미지원 빌드에서는 아무 동작도 하지 않는다.
void IsoSdk_LinGotoIdle(IsoSdkLinContext *context)
{
    (void)context;
}

// 미지원 빌드에서는 timeout 설정도 무시한다.
void IsoSdk_LinSetTimeout(IsoSdkLinContext *context, uint16_t timeout_ticks)
{
    (void)context;
    (void)timeout_ticks;
}

// 미지원 빌드에서는 timeout service도 없다.
void IsoSdk_LinServiceTick(IsoSdkLinContext *context)
{
    (void)context;
}

#endif

// 참고:
// 현재 구현은 context를 하나만 들고 있어 단일 LIN 인스턴스 전제에는 잘 맞지만,
// 여러 채널을 동시에 다룰 계획이 생기면 전역 context 구조를 분리하는 쪽이 자연스럽다.
