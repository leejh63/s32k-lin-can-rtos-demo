// 공용 LIN binding과 IsoSdk LIN 구현을 이어 주는 하드웨어 어댑터다.
// 서비스 계층은 이 파일을 통해 master/slave 역할, timeout, callback 연결만 사용한다.
#include "lin_hw.h"

#include <stddef.h>
#include <string.h>

#include "../platform/s32k_sdk/isosdk_lin.h"

typedef struct
{
    void              *event_context;
    LinHwEventBridgeFn event_bridge;
    IsoSdkLinContext   sdk_context;
    LinHwRole          role;
    uint16_t           timeout_ticks;
} LinHwState;

static LinHwState s_lin_hw;

// IsoSdk LIN 이벤트를 프로젝트 공용 LIN 하드웨어 이벤트로 바꾼다.
static LinHwEventId LinHw_EventFromIsoSdk(uint8_t event_id)
{
    switch (event_id)
    {
        case ISOSDK_LIN_EVENT_PID_OK:
            return LIN_HW_EVENT_PID_OK;

        case ISOSDK_LIN_EVENT_RX_DONE:
            return LIN_HW_EVENT_RX_DONE;

        case ISOSDK_LIN_EVENT_TX_DONE:
            return LIN_HW_EVENT_TX_DONE;

        case ISOSDK_LIN_EVENT_ERROR:
            return LIN_HW_EVENT_ERROR;

        default:
            return LIN_HW_EVENT_NONE;
    }
}

// IsoSdk callback을 공용 event bridge로 전달한다.
static void LinHw_OnIsoSdkEvent(void *context, uint8_t event_id, uint8_t current_pid)
{
    LinHwEventId lin_event_id;

    (void)context;
    lin_event_id = LinHw_EventFromIsoSdk(event_id);
    if ((s_lin_hw.event_bridge != NULL) && (lin_event_id != LIN_HW_EVENT_NONE))
    {
        s_lin_hw.event_bridge(s_lin_hw.event_context, lin_event_id, current_pid);
    }
}

// 현재 보드/빌드에서 LIN을 사용할 수 있는지 알려준다.
uint8_t LinHw_IsSupported(void)
{
    return IsoSdk_LinIsSupported();
}

// 다음 init에 사용할 LIN 역할과 timeout 기준을 미리 저장한다.
void LinHw_Configure(LinHwRole role, uint16_t timeout_ticks)
{
    (void)memset(&s_lin_hw.sdk_context, 0, sizeof(s_lin_hw.sdk_context));
    s_lin_hw.role = role;
    s_lin_hw.timeout_ticks = timeout_ticks;
    s_lin_hw.sdk_context.timeout_ticks = timeout_ticks;
}

// 하드웨어 callback이 최종적으로 넘겨야 할 상위 bridge를 연결한다.
void LinHw_AttachEventBridge(void *context, LinHwEventBridgeFn event_bridge)
{
    s_lin_hw.event_context = context;
    s_lin_hw.event_bridge = event_bridge;
}

// 저장해 둔 설정으로 실제 LIN 하드웨어를 초기화한다.
InfraStatus LinHw_Init(void *context)
{
    uint8_t iso_role;

    (void)context;
    if (IsoSdk_LinIsSupported() == 0U)
    {
        return INFRA_STATUS_UNSUPPORTED;
    }

    iso_role = (s_lin_hw.role == LIN_HW_ROLE_MASTER) ? ISOSDK_LIN_ROLE_MASTER : ISOSDK_LIN_ROLE_SLAVE;
    return (IsoSdk_LinInit(&s_lin_hw.sdk_context,
                           iso_role,
                           s_lin_hw.timeout_ticks,
                           LinHw_OnIsoSdkEvent,
                           NULL) != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// master header 송신 요청을 하드웨어 호출로 넘긴다.
InfraStatus LinHw_MasterSendHeader(void *context, uint8_t pid)
{
    (void)context;
    return (IsoSdk_LinMasterSendHeader(&s_lin_hw.sdk_context, pid) != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// 다음 data field 수신 버퍼를 하드웨어에 등록한다.
InfraStatus LinHw_StartReceive(void *context, uint8_t *buffer, uint8_t length)
{
    (void)context;
    return (IsoSdk_LinStartReceive(&s_lin_hw.sdk_context, buffer, length) != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// data field 송신 요청을 하드웨어 호출로 넘긴다.
InfraStatus LinHw_StartSend(void *context, const uint8_t *buffer, uint8_t length)
{
    (void)context;
    return (IsoSdk_LinStartSend(&s_lin_hw.sdk_context, buffer, length) != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// LIN 하드웨어 상태를 idle로 되돌린다.
void LinHw_GotoIdle(void *context)
{
    (void)context;
    IsoSdk_LinGotoIdle(&s_lin_hw.sdk_context);
}

// timeout 기준을 갱신하고 하드웨어에도 같은 값을 반영한다.
void LinHw_SetTimeout(void *context, uint16_t timeout_ticks)
{
    (void)context;
    s_lin_hw.timeout_ticks = timeout_ticks;
    IsoSdk_LinSetTimeout(&s_lin_hw.sdk_context, timeout_ticks);
}

// timer ISR 쪽에서 호출할 timeout service 한 step을 실행한다.
void LinHw_ServiceTick(void *context)
{
    (void)context;
    IsoSdk_LinServiceTick(&s_lin_hw.sdk_context);
}

// 참고:
// 현재 구현은 전역 단일 인스턴스를 전제로 해서 단순하지만,
// LIN 채널이 늘어나면 상태 보관 방식을 객체별로 바꾸는 쪽이 더 자연스럽다.
