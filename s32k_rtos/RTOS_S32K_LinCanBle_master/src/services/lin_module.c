// 공통 LIN 상태기계를 구현한 파일이다.
// protocol 흐름, 최신 상태 보관, OK token 처리를 담당하며,
// 보드 종속 동작은 binding 인터페이스를 통해 수행한다.
#include "lin_module_internal.h"

#include <stddef.h>
#include <string.h>

#include "interrupt_manager.h"

#define LIN_STATUS_WIRE_FLAGS_INDEX      4U
#define LIN_STATUS_WIRE_FLAG_VALID       (1U << 0)
#define LIN_STATUS_WIRE_FLAG_FRESH       (1U << 1)
#define LIN_STATUS_WIRE_FLAG_FAULT       (1U << 2)

// LIN 상태기계를 idle 상태로 복귀시킨다.
// 바인딩 계층에도 진행 중인 전송 상태 정리를 요청하여,
// 다음 transaction이 일관된 상태에서 시작되도록 한다.
static void LinModule_GotoIdle(LinModule *module)
{
    if (module == NULL)
    {
        return;
    }

    if (module->config.binding.goto_idle_fn != NULL)
    {
        module->config.binding.goto_idle_fn(module->config.binding.context);
    }

    module->state = LIN_MODULE_STATE_IDLE;
    module->current_pid = 0U;
}

// 현재 설정된 timeout tick 값을 하드웨어 바인딩 계층에 반영한다.
static void LinModule_SetTimeout(LinModule *module)
{
    if ((module == NULL) || (module->config.binding.set_timeout_fn == NULL))
    {
        return;
    }

    module->config.binding.set_timeout_fn(module->config.binding.context,
                                          module->config.timeout_ticks);
}

// 내부 status frame의 핵심 상태를 wire-format flag 비트로 인코딩한다.
static uint8_t LinModule_BuildStatusFlags(const LinStatusFrame *status)
{
    uint8_t flags;

    if (status == NULL)
    {
        return 0U;
    }

    flags = 0U;
    if (status->valid != 0U)
    {
        flags |= LIN_STATUS_WIRE_FLAG_VALID;
    }
    if (status->fresh != 0U)
    {
        flags |= LIN_STATUS_WIRE_FLAG_FRESH;
    }
    if (status->fault != 0U)
    {
        flags |= LIN_STATUS_WIRE_FLAG_FAULT;
    }

    return flags;
}

// 캐시된 status frame을 전송용 LIN payload buffer로 변환한다.
// slave 모드에서는 master가 status PID를 poll할 때,
// 이 buffer 내용이 응답 payload로 사용된다.
static void LinModule_PrepareStatusTx(const LinStatusFrame *status, uint8_t *buffer, uint8_t size)
{
    uint16_t adc_value;

    if ((status == NULL) || (buffer == NULL) || (size == 0U))
    {
        return;
    }

    (void)memset(buffer, 0, size);
    adc_value = status->adc_value;
    buffer[0] = (uint8_t)(adc_value & 0xFFU);
    if (size > 1U)
    {
        buffer[1] = (uint8_t)((adc_value >> 8U) & 0x0FU);
    }
    if (size > 2U)
    {
        buffer[2] = status->zone;
    }
    if (size > 3U)
    {
        buffer[3] = status->emergency_latched;
    }
    if (size > LIN_STATUS_WIRE_FLAGS_INDEX)
    {
        buffer[LIN_STATUS_WIRE_FLAGS_INDEX] = LinModule_BuildStatusFlags(status);
    }
}

// 수신한 LIN status payload를 모듈 내부 상태로 decode한다.
// master 모드에서는 결과를 최신 상태 캐시에 저장하여,
// 상위 계층이 이후 polling 주기에서 소비할 수 있게 한다.
static void LinModule_ParseStatusRx(LinModule *module, uint32_t now_ms)
{
    uint16_t adc_value;
    uint8_t  status_flags;

    if (module == NULL)
    {
        return;
    }

    adc_value = (uint16_t)module->rx_buffer[0] |
                (uint16_t)((uint16_t)module->rx_buffer[1] << 8U);
    if (adc_value > 4095U)
    {
        adc_value = 4095U;
    }

    module->latest_status.adc_value = adc_value;
    module->latest_status.zone = module->rx_buffer[2];
    module->latest_status.emergency_latched = (module->rx_buffer[3] != 0U) ? 1U : 0U;
    status_flags = LIN_STATUS_WIRE_FLAG_VALID | LIN_STATUS_WIRE_FLAG_FRESH;
    if (module->config.status_frame_size > LIN_STATUS_WIRE_FLAGS_INDEX)
    {
        status_flags = module->rx_buffer[LIN_STATUS_WIRE_FLAGS_INDEX];
    }
    module->latest_status.valid = ((status_flags & LIN_STATUS_WIRE_FLAG_VALID) != 0U) ? 1U : 0U;
    module->latest_status.fresh = ((status_flags & LIN_STATUS_WIRE_FLAG_FRESH) != 0U) ? 1U : 0U;
    module->latest_status.fault = ((status_flags & LIN_STATUS_WIRE_FLAG_FAULT) != 0U) ? 1U : 0U;
    module->latest_status_rx_ms = now_ms;
}

// callback에서 기록한 event bit와 마지막 PID를 한 번에 가져온다.
// callback과 task가 공유하는 데이터이므로 이 구간은 짧은 critical section으로 보호한다.
static uint32_t LinModule_TakePendingEvents(LinModule *module, uint8_t *out_pid)
{
    uint32_t pending_event_flags;

    if ((module == NULL) || (out_pid == NULL))
    {
        return LIN_MODULE_EVENT_FLAG_NONE;
    }

    INT_SYS_DisableIRQGlobal();
    pending_event_flags = module->pending_event_flags;
    *out_pid = module->pending_event_pid;
    module->pending_event_flags = LIN_MODULE_EVENT_FLAG_NONE;
    INT_SYS_EnableIRQGlobal();

    return pending_event_flags;
}

// callback 문맥에서 event와 PID를 최소 정보만 기록한다.
// 동시성 구간을 짧게 유지하기 위해 필요한 값만 갱신하고 즉시 반환한다.
static void LinModule_RecordEvent(LinModule *module, uint32_t event_flag, uint8_t current_pid)
{
    if (module == NULL)
    {
        return;
    }

    module->pending_event_pid = current_pid;
    module->pending_event_flags |= event_flag;
}

// 현재 transaction의 data field 수신을 시작한다.
static InfraStatus LinModule_StartReceive(LinModule *module, uint8_t *buffer, uint8_t length)
{
    if ((module == NULL) || (buffer == NULL) || (module->config.binding.start_receive_fn == NULL))
    {
        return INFRA_STATUS_NOT_READY;
    }

    if (module->config.binding.start_receive_fn(module->config.binding.context, buffer, length) != INFRA_STATUS_OK)
    {
        LinModule_GotoIdle(module);
        return INFRA_STATUS_IO_ERROR;
    }

    module->state = LIN_MODULE_STATE_WAIT_RX;
    return INFRA_STATUS_OK;
}

// 현재 transaction의 data field 송신을 시작한다.
static InfraStatus LinModule_StartSend(LinModule *module, const uint8_t *buffer, uint8_t length)
{
    if ((module == NULL) || (buffer == NULL) || (module->config.binding.start_send_fn == NULL))
    {
        return INFRA_STATUS_NOT_READY;
    }

    if (module->config.binding.start_send_fn(module->config.binding.context, buffer, length) != INFRA_STATUS_OK)
    {
        LinModule_GotoIdle(module);
        return INFRA_STATUS_IO_ERROR;
    }

    module->state = LIN_MODULE_STATE_WAIT_TX;
    return INFRA_STATUS_OK;
}

// master 역할에서 PID 인식 직후 어떤 data phase를 시작할지 결정한다.
static void LinModule_HandleMasterPidOk(LinModule *module, uint8_t current_pid)
{
    if ((module == NULL) || (module->state != LIN_MODULE_STATE_WAIT_PID))
    {
        return;
    }

    LinModule_SetTimeout(module);
    if (current_pid == module->config.pid_status)
    {
        (void)LinModule_StartReceive(module,
                                     module->rx_buffer,
                                     module->config.status_frame_size);
        return;
    }

    if (current_pid == module->config.pid_ok)
    {
        module->tx_buffer[0] = module->config.ok_token;
        if (LinModule_StartSend(module,
                                module->tx_buffer,
                                module->config.ok_frame_size) != INFRA_STATUS_OK)
        {
            module->ok_tx_pending = 0U;
        }
        return;
    }

    LinModule_GotoIdle(module);
}

// slave 역할에서 수신한 PID에 따라 status 응답 또는 OK token 수신을 준비한다.
static void LinModule_HandleSlavePidOk(LinModule *module, uint8_t current_pid)
{
    if (module == NULL)
    {
        return;
    }

    module->current_pid = current_pid;
    LinModule_SetTimeout(module);
    if (current_pid == module->config.pid_status)
    {
        LinModule_PrepareStatusTx(&module->slave_status_cache,
                                  module->tx_buffer,
                                  module->config.status_frame_size);
        (void)LinModule_StartSend(module,
                                  module->tx_buffer,
                                  module->config.status_frame_size);
        return;
    }

    if (current_pid == module->config.pid_ok)
    {
        (void)LinModule_StartReceive(module,
                                     module->rx_buffer,
                                     module->config.ok_frame_size);
        return;
    }

    LinModule_GotoIdle(module);
}

// 역할에 맞는 PID 처리 경로를 고른다.
static void LinModule_HandlePidOkEvent(LinModule *module, uint8_t current_pid)
{
    if (module == NULL)
    {
        return;
    }

    if (module->config.role == LIN_ROLE_MASTER)
    {
        LinModule_HandleMasterPidOk(module, current_pid);
        return;
    }

    LinModule_HandleSlavePidOk(module, current_pid);
}

// transaction 중 에러가 나면 필요한 pending 상태를 정리하고 idle로 돌아간다.
static void LinModule_HandleErrorEvent(LinModule *module)
{
    if (module == NULL)
    {
        return;
    }

    if ((module->config.role == LIN_ROLE_MASTER) &&
        (module->current_pid == module->config.pid_ok))
    {
        module->ok_tx_pending = 0U;
    }

    LinModule_GotoIdle(module);
}

// RX 완료 후 status decode나 ok token 소비 표시를 처리한다.
static void LinModule_HandleRxDoneEvent(LinModule *module, uint32_t now_ms)
{
    if (module == NULL)
    {
        return;
    }

    if ((module->config.role == LIN_ROLE_MASTER) &&
        (module->state == LIN_MODULE_STATE_WAIT_RX))
    {
        LinModule_ParseStatusRx(module, now_ms);
        LinModule_GotoIdle(module);
        return;
    }

    if ((module->config.role == LIN_ROLE_SLAVE) &&
        (module->current_pid == module->config.pid_ok) &&
        (module->rx_buffer[0] == module->config.ok_token))
    {
        module->ok_token_pending = 1U;
    }

    LinModule_GotoIdle(module);
}

// TX 완료 후 pending 상태를 정리하고 idle로 돌아간다.
static void LinModule_HandleTxDoneEvent(LinModule *module)
{
    if (module == NULL)
    {
        return;
    }

    if ((module->config.role == LIN_ROLE_MASTER) &&
        (module->state == LIN_MODULE_STATE_WAIT_TX))
    {
        module->ok_tx_pending = 0U;
    }

    LinModule_GotoIdle(module);
}

// 최근 상태가 아직 유효한 값인지 확인한다.
static uint8_t LinModule_IsStatusFresh(const LinModule *module,
                                       uint32_t now_ms,
                                       uint32_t max_age_ms)
{
    if ((module == NULL) ||
        ((module->latest_status.valid == 0U) && (module->latest_status.fault == 0U)))
    {
        return 0U;
    }

    if (max_age_ms == 0U)
    {
        return 1U;
    }

    return (Infra_TimeIsExpired(now_ms, module->latest_status_rx_ms, max_age_ms) == 0U) ? 1U : 0U;
}

// header를 보내서 master 측 LIN transaction 하나를 시작한다.
// 이 함수는 idle 상태에서만 성공하도록 제한해,
// 상태기계가 한 번에 하나의 status/token 교환만 다루게 한다.
static InfraStatus LinModule_MasterStart(LinModule *module, uint8_t pid)
{
    if ((module == NULL) || (module->config.binding.master_send_header_fn == NULL))
    {
        return INFRA_STATUS_NOT_READY;
    }

    if (module->state != LIN_MODULE_STATE_IDLE)
    {
        return INFRA_STATUS_BUSY;
    }

    if (module->config.binding.master_send_header_fn(module->config.binding.context, pid) != INFRA_STATUS_OK)
    {
        LinModule_GotoIdle(module);
        return INFRA_STATUS_IO_ERROR;
    }

    module->current_pid = pid;
    module->state = LIN_MODULE_STATE_WAIT_PID;
    return INFRA_STATUS_OK;
}

// 공통 LIN 상태기계를 초기화한다.
// 먼저 설정을 복사하고,
// 그다음 하드웨어 바인딩에 실제 주변장치 초기화를 요청한다.
InfraStatus LinModule_Init(LinModule *module, const LinConfig *config)
{
    InfraStatus status;

    if ((module == NULL) || (config == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(module, 0, sizeof(*module));
    module->config = *config;
    module->state = LIN_MODULE_STATE_IDLE;

    if (module->config.binding.init_fn == NULL)
    {
        return INFRA_STATUS_NOT_READY;
    }

    status = module->config.binding.init_fn(module->config.binding.context);
    if (status != INFRA_STATUS_OK)
    {
        return status;
    }

    module->initialized = 1U;
    return INFRA_STATUS_OK;
}

// timer base tick에서 timeout service를 한 번 진행한다.
void LinModule_OnBaseTick(LinModule *module)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    if (module->config.binding.service_tick_fn != NULL)
    {
        module->config.binding.service_tick_fn(module->config.binding.context);
    }
}

// runtime_io가 전달한 하드웨어 event 하나를 기록한다.
// callback 경계에서는 project event와 PID만 저장하고,
// 실제 RX/TX 시작과 decode 처리는 fast task가 맡는다.
// NOTE:
// 현재 구현은 pending_event_flags + pending_event_pid 한 슬롯만 사용한다.
// 지금 밀도에서는 충분하지만, event burst나 순서 보존 요구가 커지면 queue화가 더 안전하다.
void LinModule_OnEvent(LinModule *module, LinEventId event_id, uint8_t current_pid)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    // callback에서는 무거운 일을 하지 않고, 최소 event 정보만 coalesce해서 남긴다.
    switch (event_id)
    {
        case LIN_EVENT_PID_OK:
            LinModule_RecordEvent(module, LIN_MODULE_EVENT_FLAG_PID_OK, current_pid);
            break;

        case LIN_EVENT_RX_DONE:
            LinModule_RecordEvent(module, LIN_MODULE_EVENT_FLAG_RX_DONE, current_pid);
            break;

        case LIN_EVENT_TX_DONE:
            LinModule_RecordEvent(module, LIN_MODULE_EVENT_FLAG_TX_DONE, current_pid);
            break;

        case LIN_EVENT_ERROR:
            LinModule_RecordEvent(module, LIN_MODULE_EVENT_FLAG_ERROR, current_pid);
            break;

        default:
            break;
    }
}

// LIN 상태기계를 빠르게 진행시킨다.
// 기록된 PID, RX, TX, error event에 반응하여,
// callback 경계 밖에서 RX/TX 시작과 decode를 이어서 처리한다.
// NOTE:
// fast task는 ISR 후속처리 전용이다. poll 정책과 섞이지 않도록 유지해야
// LIN의 즉시 반응 경로와 주기 정책 경로를 읽기 쉽게 분리할 수 있다.
void LinModule_TaskFast(LinModule *module, uint32_t now_ms)
{
    uint32_t pending_event_flags;
    uint8_t  pending_event_pid;

    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    // 1) callback이 남긴 event snapshot을 한 번에 가져온다.
    pending_event_flags = LinModule_TakePendingEvents(module, &pending_event_pid);
    if (pending_event_flags == LIN_MODULE_EVENT_FLAG_NONE)
    {
        return;
    }

    // 2) error는 가장 우선해서 현재 transaction을 정리한다.
    if ((pending_event_flags & LIN_MODULE_EVENT_FLAG_ERROR) != 0U)
    {
        LinModule_HandleErrorEvent(module);
        return;
    }

    // 3) PID/RX/TX 후속처리는 event 종류별로 짧게 이어서 처리한다.
    if ((pending_event_flags & LIN_MODULE_EVENT_FLAG_PID_OK) != 0U)
    {
        LinModule_HandlePidOkEvent(module, pending_event_pid);
    }

    if ((pending_event_flags & LIN_MODULE_EVENT_FLAG_RX_DONE) != 0U)
    {
        LinModule_HandleRxDoneEvent(module, now_ms);
    }

    if ((pending_event_flags & LIN_MODULE_EVENT_FLAG_TX_DONE) != 0U)
    {
        LinModule_HandleTxDoneEvent(module);
    }
}

// 주기적인 master 측 LIN 작업을 시작한다.
// poll task는 현재 application 필요에 따라,
// status 요청과 queue에 쌓인 OK token 전송 중 하나를 선택한다.
// NOTE:
// 현재 poll 슬롯은 status와 ok가 공유한다. 구조는 단순하지만 한 poll에 하나만 진행되므로
// throughput을 올려야 하는 상황에서는 schedule 재설계가 필요할 수 있다.
void LinModule_TaskPoll(LinModule *module, uint32_t now_ms)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    if (module->config.role != LIN_ROLE_MASTER)
    {
        return;
    }

    // 1) poll period가 되었는지 확인한다.
    if (Infra_TimeIsDue(now_ms, module->last_poll_ms, module->config.poll_period_ms) == 0U)
    {
        return;
    }

    module->last_poll_ms = now_ms;

    // 2) OK token pending이 있으면 status보다 우선해서 그 슬롯을 사용한다.
    if (module->ok_tx_pending != 0U)
    {
        (void)LinModule_MasterStart(module, module->config.pid_ok);
        return;
    }

    // 3) 별도 pending이 없으면 기본 status poll을 시작한다.
    (void)LinModule_MasterStart(module, module->config.pid_status);
}

// 다음 master poll 슬롯에 보낼 OK token 전송을 queue에 적재한다.
// 승인 로직은 즉시 보내지 않고 이 경로를 사용해,
// 버스 동작이 일반 상태기계 흐름 안에서 진행되게 한다.
InfraStatus LinModule_RequestOk(LinModule *module)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (module->config.role != LIN_ROLE_MASTER)
    {
        return INFRA_STATUS_UNSUPPORTED;
    }

    if (module->ok_tx_pending != 0U)
    {
        return INFRA_STATUS_BUSY;
    }

    module->ok_tx_pending = 1U;
    module->tx_buffer[0] = module->config.ok_token;
    return INFRA_STATUS_OK;
}

// 최신 status가 아직 유효한 나이인지 확인한 뒤 호출자에게 복사한다.
InfraStatus LinModule_GetLatestStatusIfFresh(const LinModule *module,
                                             uint32_t now_ms,
                                             uint32_t max_age_ms,
                                             LinStatusFrame *out_status)
{
    if ((module == NULL) || (out_status == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if ((module->latest_status.valid == 0U) && (module->latest_status.fault == 0U))
    {
        return INFRA_STATUS_EMPTY;
    }

    if (LinModule_IsStatusFresh(module, now_ms, max_age_ms) == 0U)
    {
        return INFRA_STATUS_TIMEOUT;
    }

    *out_status = module->latest_status;
    return INFRA_STATUS_OK;
}

// 아직 소비되지 않은 최신 상태 하나를 한 번만 꺼내 준다.
uint8_t LinModule_ConsumeFreshStatus(LinModule *module, LinStatusFrame *out_status)
{
    if ((module == NULL) || (out_status == NULL) || (module->latest_status.fresh == 0U))
    {
        return 0U;
    }

    *out_status = module->latest_status;
    module->latest_status.fresh = 0U;
    return 1U;
}

// slave가 다음 status poll 때 내보낼 status cache를 갱신한다.
void LinModule_SetSlaveStatus(LinModule *module, const LinStatusFrame *status)
{
    if ((module == NULL) || (status == NULL) || (module->initialized == 0U))
    {
        return;
    }

    module->slave_status_cache = *status;
    module->slave_status_cache.emergency_latched = (status->emergency_latched != 0U) ? 1U : 0U;
    module->slave_status_cache.valid = (status->valid != 0U) ? 1U : 0U;
    module->slave_status_cache.fresh = 1U;
    module->slave_status_cache.fault = (status->fault != 0U) ? 1U : 0U;
}

// slave가 수신한 ok token pending 상태를 한 번만 소비한다.
uint8_t LinModule_ConsumeSlaveOkToken(LinModule *module)
{
    uint8_t pending;

    if (module == NULL)
    {
        return 0U;
    }

    pending = module->ok_token_pending;
    module->ok_token_pending = 0U;
    return pending;
}

// 참고:
// event를 bit flag와 마지막 PID 한 슬롯으로만 들고 있어서 현재 구조에는 충분하지만,
// 이벤트 밀도가 더 높아지면 queue 형태로 바꾸는 쪽이 맥락 손실을 줄이는 데 도움이 된다.
