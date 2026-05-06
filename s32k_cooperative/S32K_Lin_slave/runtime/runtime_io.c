// LIN sensor slave용 프로젝트 바인딩 구현 파일이다.
// 역할별 상수와 주기 설정을 조립하고,
// 실제 하드웨어 접근은 shared driver 계층에 위임한다.
#include "runtime_io.h"

#include <stddef.h>
#include <string.h>

#include "../drivers/adc_hw.h"
#include "../drivers/board_hw.h"
#include "../drivers/lin_hw.h"
#include "../app/app_config.h"

#define RUNTIME_IO_MASTER_LIN_ADC_PID         0x24U
#define RUNTIME_IO_MASTER_LIN_OK_PID          0x25U
#define RUNTIME_IO_MASTER_LIN_OK_TOKEN        0xA5U
#define RUNTIME_IO_MASTER_LIN_TIMEOUT_TICKS   100U

#define RUNTIME_IO_ADC_RANGE_MAX              4096U
#define RUNTIME_IO_ADC_SAFE_MAX               (RUNTIME_IO_ADC_RANGE_MAX / 3U)
#define RUNTIME_IO_ADC_WARNING_MAX            ((RUNTIME_IO_ADC_RANGE_MAX * 2U) / 3U)
#define RUNTIME_IO_ADC_EMERGENCY_MIN          ((RUNTIME_IO_ADC_RANGE_MAX * 5U) / 6U)

// 하드웨어 LIN 이벤트 값을 공용 LIN 모듈 이벤트 값으로 바꾼다.
static LinEventId RuntimeIo_LinEventFromHw(LinHwEventId event_id)
{
    switch (event_id)
    {
        case LIN_HW_EVENT_PID_OK:
            return LIN_EVENT_PID_OK;

        case LIN_HW_EVENT_RX_DONE:
            return LIN_EVENT_RX_DONE;

        case LIN_HW_EVENT_TX_DONE:
            return LIN_EVENT_TX_DONE;

        case LIN_HW_EVENT_ERROR:
            return LIN_EVENT_ERROR;

        case LIN_HW_EVENT_NONE:
        default:
            return LIN_EVENT_NONE;
    }
}

// 하드웨어 LIN callback을 공용 LIN 모듈 이벤트로 이어 준다.
static void RuntimeIo_HandleLinHwEvent(void *context,
                                       LinHwEventId event_id,
                                       uint8_t current_pid)
{
    LinEventId lin_event_id;

    if (context == NULL)
    {
        return;
    }

    lin_event_id = RuntimeIo_LinEventFromHw(event_id);
    if (lin_event_id == LIN_EVENT_NONE)
    {
        return;
    }

    LinModule_OnEvent((LinModule *)context, lin_event_id, current_pid);
}

// sensor slave 보드 공통 초기화를 수행하고 LIN 트랜시버를 켠다.
InfraStatus RuntimeIo_BoardInit(void)
{
    InfraStatus status;

    status = BoardHw_Init();
    if (status != INFRA_STATUS_OK)
    {
        return status;
    }

    BoardHw_EnableLinTransceiver();
    return INFRA_STATUS_OK;
}

// 이 프로젝트에서 쓰는 로컬 node id를 반환한다.
uint8_t RuntimeIo_GetLocalNodeId(void)
{
    return APP_NODE_ID_SLAVE2;
}

// slave2가 쓸 LED 배선 설정을 공용 형태로 반환한다.
InfraStatus RuntimeIo_GetSlave2LedConfig(LedConfig *out_config)
{
    return BoardHw_GetRgbLedConfig(out_config);
}

// slave2 ADC 샘플링 모듈이 바로 쓸 수 있는 설정을 조립한다.
// threshold와 주기도 여기서 한 번에 정리해 두어,
// app/service 계층은 의미 중심 코드만 보게 만든다.
InfraStatus RuntimeIo_GetSlave2AdcConfig(AdcConfig *out_config)
{
    if (out_config == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(out_config, 0, sizeof(*out_config));
    out_config->init_fn = AdcHw_Init;
    out_config->sample_fn = AdcHw_Sample;
    out_config->hw_context = NULL;
    out_config->sample_period_ms = APP_TASK_ADC_MS;
    out_config->range_max = RUNTIME_IO_ADC_RANGE_MAX;
    out_config->safe_max = RUNTIME_IO_ADC_SAFE_MAX;
    out_config->warning_max = RUNTIME_IO_ADC_WARNING_MAX;
    out_config->emergency_min = RUNTIME_IO_ADC_EMERGENCY_MIN;
    return (AdcHw_IsSupported() != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_UNSUPPORTED;
}

// LIN 하드웨어 이벤트를 공용 LIN 모듈로 이어 준다.
void RuntimeIo_AttachLinModule(LinModule *module)
{
    LinHw_AttachEventBridge(module, RuntimeIo_HandleLinHwEvent);
}

// slave 역할에 맞는 LIN PID, token, binding 설정을 한곳에서 조립한다.
InfraStatus RuntimeIo_GetSlaveLinConfig(LinConfig *out_config)
{
    if (out_config == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(out_config, 0, sizeof(*out_config));
    out_config->role = LIN_ROLE_SLAVE;
    out_config->pid_status = RUNTIME_IO_MASTER_LIN_ADC_PID;
    out_config->pid_ok = RUNTIME_IO_MASTER_LIN_OK_PID;
    out_config->ok_token = RUNTIME_IO_MASTER_LIN_OK_TOKEN;
    out_config->status_frame_size = 8U;
    out_config->ok_frame_size = 1U;
    out_config->timeout_ticks = RUNTIME_IO_MASTER_LIN_TIMEOUT_TICKS;
    out_config->poll_period_ms = APP_TASK_LIN_POLL_MS;
    out_config->binding.init_fn = LinHw_Init;
    out_config->binding.master_send_header_fn = LinHw_MasterSendHeader;
    out_config->binding.start_receive_fn = LinHw_StartReceive;
    out_config->binding.start_send_fn = LinHw_StartSend;
    out_config->binding.goto_idle_fn = LinHw_GotoIdle;
    out_config->binding.set_timeout_fn = LinHw_SetTimeout;
    out_config->binding.service_tick_fn = LinHw_ServiceTick;
    out_config->binding.context = NULL;

    LinHw_Configure(LIN_HW_ROLE_SLAVE, RUNTIME_IO_MASTER_LIN_TIMEOUT_TICKS);
    return (LinHw_IsSupported() != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_UNSUPPORTED;
}

// 참고:
// ADC threshold와 LIN PID 상수가 한 파일에 모여 있어 찾기는 쉽지만,
// 센서 종류가 더 늘어나면 역할별 설정 묶음을 조금 더 구조화해 두는 편이 가독성이 높다.
