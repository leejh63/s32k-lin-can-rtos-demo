// CAN 현장 반응 slave 최소 운영 구성용 애플리케이션 구현 파일이다.
// slave1에 필요한 CAN 수신/응답, 버튼 debounce, LED 상태기계와
// 최소 진단 상태만 유지해 애플리케이션 경계를 명확히 한다.
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"

#include "app_config.h"
#include "app_core_internal.h"
#include "app_slave1.h"

// 짧은 상태 문자열을 안전하게 복사해 둔다.
// 화면에 보이는 텍스트는 크기가 작고 자주 바뀌므로,
// 이 보조 함수 하나로 null 종료와 길이 제한을 함께 맞춘다.
static void AppCore_SetTextBuffer(char *buffer, size_t size, const char *text)
{
    size_t text_length;

    if ((buffer == NULL) || (size == 0U) || (text == NULL))
    {
        return;
    }

    text_length = strlen(text);
    if (text_length >= size)
    {
        text_length = size - 1U;
    }

    (void)memcpy(buffer, text, text_length);
    buffer[text_length] = '\0';
}

// app 계층에서 공유 상태를 짧게 보호하기 위한 보조 함수다.
// 이번 단계는 mutex보다 작은 critical section이 더 단순하므로,
// task context에서만 짧게 감싸는 용도로 사용한다.
void AppCore_EnterCritical(void)
{
    taskENTER_CRITICAL();
}

// AppCore_EnterCritical()와 짝을 이루는 종료 함수다.
void AppCore_ExitCritical(void)
{
    taskEXIT_CRITICAL();
}

// 현재 동작 모드를 사람이 읽기 쉬운 짧은 말로 남긴다.
// mode 문자열은 render나 진단에서 그대로 쓰이므로,
// 상태가 바뀌는 지점마다 간단히 갱신해 둔다.
void AppCore_SetModeText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_EnterCritical();
        AppCore_SetTextBuffer(app->mode_text, sizeof(app->mode_text), text);
        AppCore_ExitCritical();
    }
}

// 버튼 쪽 상태 문구를 갱신한다.
// 실제 입력 값 자체보다 운영자에게 보여 줄 의미를 남기는 용도라,
// 눌림, 대기, 승인 같은 짧은 표현만 담는다.
void AppCore_SetButtonText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_EnterCritical();
        AppCore_SetTextBuffer(app->button_text, sizeof(app->button_text), text);
        AppCore_ExitCritical();
    }
}

// CAN 입력이나 전송 결과를 한 줄 텍스트로 남긴다.
// 최근에 들어온 명령이나 timeout 같은 상황을 바로 읽을 수 있게,
// app 쪽에서 의미를 붙인 뒤 이 버퍼에 적어 둔다.
void AppCore_SetCanInputText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_EnterCritical();
        AppCore_SetTextBuffer(app->can_input_text, sizeof(app->can_input_text), text);
        AppCore_ExitCritical();
    }
}

// CAN 요청 제출 실패를 진단 정보에 간단히 적어 둔다.
// 어떤 명령이 막혔는지 정도만 남겨도
// 현장 데모나 bring-up 때 흐름을 따라가기가 한결 수월하다.
static void AppCore_RecordCanRequestFailure(AppCore *app, uint8_t command_code)
{
    if (app == NULL)
    {
        return;
    }

    AppCore_EnterCritical();
    app->diag.can_send_fail_count++;
    app->diag.last_can_result_kind = CAN_SERVICE_RESULT_SEND_FAIL;
    app->diag.last_can_result_code = CAN_RES_FAIL;
    app->diag.last_can_detail_code = 0U;
    app->diag.last_can_command_code = command_code;
    AppCore_ExitCritical();
}

// 시작 직후 보일 기본 문구를 채워 둔다.
// 첫 수신이나 첫 버튼 입력 전에도
// 현재 화면이 비어 보이지 않게 최소한의 상태를 먼저 마련한다.
static void AppCore_InitializeDefaultViewText(AppCore *app)
{
    if (app == NULL)
    {
        return;
    }

    AppCore_SetModeText(app, "boot");
    AppCore_SetButtonText(app, "ready");
    AppCore_SetCanInputText(app, "waiting");
}

// service에서 올라온 CAN 결과를 진단 정보와 화면 문구에 반영한다.
// 응답, timeout, 전송 실패를 한군데에서 정리하면
// 상위 task는 결과를 소비하는 작업에만 집중할 수 있다.
static void AppCore_RecordCanServiceResult(AppCore *app, const CanServiceResult *result)
{
    char buffer[48];
    uint8_t update_text;

    if ((app == NULL) || (result == NULL))
    {
        return;
    }

    update_text = 0U;
    switch (result->kind)
    {
        case CAN_SERVICE_RESULT_RESPONSE:
            (void)snprintf(buffer,
                           sizeof(buffer),
                           "rsp %u from %u",
                           (unsigned int)result->result_code,
                           (unsigned int)result->source_node_id);
            update_text = 1U;
            break;

        case CAN_SERVICE_RESULT_TIMEOUT:
            (void)snprintf(buffer,
                           sizeof(buffer),
                           "timeout cmd %u",
                           (unsigned int)result->command_code);
            update_text = 1U;
            break;

        case CAN_SERVICE_RESULT_SEND_FAIL:
            (void)snprintf(buffer,
                           sizeof(buffer),
                           "tx fail cmd %u",
                           (unsigned int)result->command_code);
            update_text = 1U;
            break;

        default:
            break;
    }

    AppCore_EnterCritical();
    app->diag.last_can_result_kind = result->kind;
    app->diag.last_can_result_code = result->result_code;
    app->diag.last_can_detail_code = result->detail_code;
    app->diag.last_can_command_code = result->command_code;

    switch (result->kind)
    {
        case CAN_SERVICE_RESULT_RESPONSE:
            app->diag.can_response_count++;
            break;

        case CAN_SERVICE_RESULT_TIMEOUT:
            app->diag.can_timeout_count++;
            break;

        case CAN_SERVICE_RESULT_SEND_FAIL:
            app->diag.can_send_fail_count++;
            break;

        default:
            break;
    }
    AppCore_ExitCritical();

    if (update_text != 0U)
    {
        AppCore_SetCanInputText(app, buffer);
    }
}

// 하위 CAN 계층에서 모은 진단 값을 app 진단 상태와 화면 문구로 끌어올린다.
// 지금 단계에서는 "에러가 나는지"와 "RX/TX가 실제로 움직이는지"가 중요하므로,
// 하드웨어 카운터를 짧은 텍스트 한 줄로 바로 보이게 만든다.
static void AppCore_UpdateCanHardwareDiag(AppCore *app)
{
    CanModuleDiag can_diag;
    uint8_t       changed;
    char          buffer[48];

    if ((app == NULL) || (app->can_enabled == 0U))
    {
        return;
    }

    CanModule_GetDiag(&app->can_module, &can_diag);
    AppCore_EnterCritical();
    changed = 0U;
    if ((app->diag.can_service_last_error != can_diag.service_last_error) ||
        (app->diag.can_transport_last_error != can_diag.transport_last_error) ||
        (app->diag.can_hw_last_error != can_diag.hw_last_error) ||
        (app->diag.can_hw_tx_ok_count != can_diag.hw_tx_ok_count) ||
        (app->diag.can_hw_tx_error_count != can_diag.hw_tx_error_count) ||
        (app->diag.can_hw_rx_ok_count != can_diag.hw_rx_ok_count) ||
        (app->diag.can_hw_rx_error_count != can_diag.hw_rx_error_count) ||
        (app->diag.can_hw_rx_drop_count != can_diag.hw_rx_drop_count))
    {
        changed = 1U;
    }

    app->diag.can_service_last_error = can_diag.service_last_error;
    app->diag.can_transport_last_error = can_diag.transport_last_error;
    app->diag.can_hw_last_error = can_diag.hw_last_error;
    app->diag.can_hw_tx_ok_count = can_diag.hw_tx_ok_count;
    app->diag.can_hw_tx_error_count = can_diag.hw_tx_error_count;
    app->diag.can_hw_rx_ok_count = can_diag.hw_rx_ok_count;
    app->diag.can_hw_rx_error_count = can_diag.hw_rx_error_count;
    app->diag.can_hw_rx_drop_count = can_diag.hw_rx_drop_count;
    AppCore_ExitCritical();

    if (changed == 0U)
    {
        return;
    }

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "h%u r%lu/%lu t%lu/%lu",
                   (unsigned int)can_diag.hw_last_error,
                   (unsigned long)can_diag.hw_rx_error_count,
                   (unsigned long)can_diag.hw_rx_ok_count,
                   (unsigned long)can_diag.hw_tx_error_count,
                   (unsigned long)can_diag.hw_tx_ok_count);
    AppCore_SetCanInputText(app, buffer);
}

// 버튼 처리 경로에서 예약한 OK 요청을 pending 상태로 전이한다.
// 실제 CAN queue 제출은 다음 task tick에 맡겨 두고,
// 여기서는 중복 요청만 막는 작은 상태 전이만 담당한다.
InfraStatus AppCore_RequestSlave1Ok(AppCore *app)
{
    InfraStatus status;

    if (app == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (app->can_enabled == 0U)
    {
        return INFRA_STATUS_NOT_READY;
    }

    AppCore_EnterCritical();
    status = INFRA_STATUS_OK;
    if (app->slave1_mode != APP_SLAVE1_MODE_EMERGENCY)
    {
        status = INFRA_STATUS_NOT_READY;
    }
    if (app->slave1_ok_request_state != APP_SLAVE1_OK_REQUEST_IDLE)
    {
        status = INFRA_STATUS_BUSY;
    }
    else if (status == INFRA_STATUS_OK)
    {
        app->slave1_ok_request_state = APP_SLAVE1_OK_REQUEST_WAIT_CAN_QUEUE;
    }
    AppCore_ExitCritical();
    return status;
}

// 버튼에서 예약해 둔 OK 요청을 CAN queue로 실제 제출해 본다.
// queue가 잠시 가득 찬 정도라면 상태를 유지한 채 다음 tick을 기다리고,
// 더 진행하기 어려운 실패만 바로 진단으로 남긴다.
// 주의: 현재 이 로컬 OK 요청은 need_response 없이 올라가므로,
// master가 실제로 받았는지에 대한 왕복 추적과 timeout 결과는 남기지 않는다.
static void AppCore_SubmitPendingSlave1OkRequest(AppCore *app)
{
    InfraStatus status;
    uint8_t     request_pending;

    if ((app == NULL) || (app->can_enabled == 0U))
    {
        return;
    }

    AppCore_EnterCritical();
    request_pending = (app->slave1_ok_request_state == APP_SLAVE1_OK_REQUEST_WAIT_CAN_QUEUE) ? 1U : 0U;
    AppCore_ExitCritical();
    if (request_pending == 0U)
    {
        return;
    }

    // local 버튼 이벤트를 실제 CAN command로 바꾸는 지점이다.
    // 현재는 master에게 OK command를 보내기만 하고,
    // 별도 response를 요구하지 않는 fire-and-forget 흐름으로 동작한다.
    status = CanModule_QueueCommand(&app->can_module,
                                    APP_NODE_ID_MASTER,
                                    CAN_CMD_OK,
                                    0U,
                                    0U,
                                    0U);
    if (status == INFRA_STATUS_OK)
    {
        AppCore_EnterCritical();
        if (app->slave1_ok_request_state == APP_SLAVE1_OK_REQUEST_WAIT_CAN_QUEUE)
        {
            app->slave1_ok_request_state = APP_SLAVE1_OK_REQUEST_IDLE;
        }
        AppCore_ExitCritical();
        AppCore_SetCanInputText(app, "ok req queued");
        return;
    }

    if (status == INFRA_STATUS_FULL)
    {
        AppCore_SetCanInputText(app, "ok req pending");
        return;
    }

    AppCore_EnterCritical();
    if (app->slave1_ok_request_state == APP_SLAVE1_OK_REQUEST_WAIT_CAN_QUEUE)
    {
        app->slave1_ok_request_state = APP_SLAVE1_OK_REQUEST_IDLE;
    }
    AppCore_ExitCritical();
    AppCore_RecordCanRequestFailure(app, CAN_CMD_OK);
    AppCore_SetCanInputText(app, "ok req failed");
}

// AppCore가 쓰는 CAN module을 로컬 node 설정으로 준비한다.
// app 쪽에서 필요한 timeout과 제출량 제한도 함께 넘겨,
// 이후 task 코드가 별도 setup 없이 바로 통신을 시작하게 한다.
InfraStatus AppCore_InitCanModule(AppCore *app)
{
    CanModuleConfig can_config;

    if (app == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(&can_config, 0, sizeof(can_config));
    can_config.local_node_id = app->local_node_id;
    can_config.default_timeout_ms = app->can_default_timeout_ms;
    can_config.max_submit_per_tick = app->can_max_submit_per_tick;
    if (CanModule_Init(&app->can_module, &can_config) != INFRA_STATUS_OK)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    app->can_enabled = 1U;
    app->diag.can_service_last_error = 0xFFU;
    app->diag.can_transport_last_error = 0xFFU;
    app->diag.can_hw_last_error = 0xFFU;
    return INFRA_STATUS_OK;
}

// 들어온 CAN command를 slave1 로컬 동작으로 바꿔 처리한다.
// command 해석은 정책 함수에 넘기고,
// 응답이 필요한 경우에만 같은 request id로 결과를 되돌려 보낸다.
// 주의: 현재 AppCore는 command만 직접 처리한다.
// event/text는 service까지는 올라오지만, 이 함수에서는 별도 정책 없이 통과하지 않는다.
static void AppCore_ProcessIncomingCanCommand(AppCore *app, const CanMessage *message)
{
    uint8_t response_code;

    if ((app == NULL) || (message == NULL))
    {
        return;
    }

    if (message->message_type != CAN_MSG_COMMAND)
    {
        // 현재 app 정책은 command만 처리한다.
        // event/text를 활용하게 되면 이 지점 또는 별도 분기 함수가 확장 포인트가 된다.
        return;
    }

    // command payload 해석과 로컬 상태 전이는 역할 정책 함수가 맡는다.
    response_code = CAN_RES_NOT_SUPPORTED;
    AppSlave1_HandleCanCommand(app, message, &response_code);

    if ((message->flags & CAN_MSG_FLAG_NEED_RESPONSE) != 0U)
    {
        if (CanModule_QueueResponse(&app->can_module,
                                    message->source_node_id,
                                    message->request_id,
                                    response_code,
                                    0U) != INFRA_STATUS_OK)
        {
            AppCore_RecordCanRequestFailure(app, message->payload[0]);
            AppCore_SetCanInputText(app, "rsp queue full");
        }
    }
}

// 완료된 CAN 결과를 정해 둔 수만큼 꺼내 처리한다.
// 한 tick에 모든 결과를 다 비우려 들지 않아야
// 다른 주기 작업이 오래 묶이지 않는다.
static void AppCore_ProcessCanResults(AppCore *app)
{
    CanServiceResult result;
    uint8_t          processed_count;

    if (app == NULL)
    {
        return;
    }

    processed_count = 0U;
    while (processed_count < APP_CAN_MAX_RESULTS_PER_TICK)
    {
        if (CanModule_TryPopResult(&app->can_module, &result) == 0U)
        {
            break;
        }

        // response/timeout/send-fail을 같은 소비 경로로 처리한다.
        AppCore_RecordCanServiceResult(app, &result);
        processed_count++;
    }
}

// 수신 queue에 쌓인 CAN 메시지를 조금씩 app 정책으로 넘긴다.
// 입력량이 몰려도 한 번에 과하게 오래 머물지 않도록
// tick당 처리량을 작게 제한해 둔다.
static void AppCore_ProcessIncomingCanMessages(AppCore *app)
{
    CanMessage message;
    uint8_t    processed_count;

    if (app == NULL)
    {
        return;
    }

    processed_count = 0U;
    while (processed_count < APP_CAN_MAX_INCOMING_PER_TICK)
    {
        if (CanModule_TryPopIncoming(&app->can_module, &message) == 0U)
        {
            break;
        }

        // service가 target과 protocol을 한 번 걸러 준 메시지만 여기 도착한다.
        AppCore_ProcessIncomingCanCommand(app, &message);
        processed_count++;
    }
}

// 애플리케이션 공용 상태와 slave1 역할 구성을 한 번에 초기화한다.
// 기본 화면 문구를 먼저 맞춘 뒤,
// 역할별 init이 성공했을 때만 정상 실행 상태로 들어간다.
InfraStatus AppCore_Init(AppCore *app, const AppCoreConfig *config)
{
    InfraStatus status;

    if ((app == NULL) || (config == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if ((config->local_node_id < CAN_NODE_ID_MIN) ||
        (config->local_node_id > CAN_NODE_ID_MAX))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(app, 0, sizeof(*app));
    app->local_node_id = config->local_node_id;
    app->can_default_timeout_ms = (config->can_default_timeout_ms != 0U) ?
                                  config->can_default_timeout_ms : 300U;
    app->can_max_submit_per_tick = (config->can_max_submit_per_tick != 0U) ?
                                   config->can_max_submit_per_tick : 2U;
    app->slave1_mode = APP_SLAVE1_MODE_BOOT;
    app->slave1_ok_request_state = APP_SLAVE1_OK_REQUEST_IDLE;
    AppCore_InitializeDefaultViewText(app);

    status = AppSlave1_Init(app);
    if (status != INFRA_STATUS_OK)
    {
        return status;
    }

    app->initialized = 1U;
    return INFRA_STATUS_OK;
}

// 살아 있음을 나타내는 heartbeat 카운터를 증가시킨다.
// 현재는 단순한 카운터지만,
// 추후 상태 표시나 watchdog 진단과도 쉽게 연결할 수 있는 자리다.
void AppCore_TaskHeartbeat(AppCore *app, uint32_t now_ms)
{
    (void)now_ms;

    if (app != NULL)
    {
        app->heartbeat_count++;
    }
}

// CAN 쪽 주기 작업을 한 번 진행한다.
// 예약된 요청 제출, transport 진행, 결과 소비, 수신 command 처리까지
// CAN 관련 흐름을 이 task 한 번에 이어서 묶어 둔다.
// 주의: 현재 구조는 한 task 안에 제출/진행/소비가 함께 들어 있어 읽기 쉽지만,
// 역할이 더 늘어나면 송신·수신·결과 반영을 나눌지 다시 볼 여지가 있다.
void AppCore_TaskCan(AppCore *app, uint32_t now_ms)
{
    if ((app == NULL) || (app->can_enabled == 0U))
    {
        return;
    }

    // 1) 이전 tick에서 예약된 local OK 요청을 먼저 한 번 밀어 본다.
    AppCore_SubmitPendingSlave1OkRequest(app);

    // 2) transport 진행, RX decode, timeout 확인, module queue 제출을 수행한다.
    CanModule_Task(&app->can_module, now_ms);

    // 3) 방금 전 주기 처리로 queue 여유가 생겼을 수 있으니 한 번 더 시도한다.
    AppCore_SubmitPendingSlave1OkRequest(app);

    // 4) service가 만들어 둔 결과와 수신 메시지를 제한된 개수만큼 소비한다.
    AppCore_ProcessCanResults(app);
    AppCore_ProcessIncomingCanMessages(app);

    // 5) 현재 CAN 하드웨어가 실제로 RX/TX를 진행하는지 진단 문자열로 남긴다.
    AppCore_UpdateCanHardwareDiag(app);
}

// 버튼 관련 정책을 slave1 역할 코드로 넘긴다.
// AppCore는 task 진입점만 유지하고,
// debounce와 역할별 반응은 별도 정책 함수가 맡는다.
void AppCore_TaskButton(AppCore *app, uint32_t now_ms)
{
    if (app == NULL)
    {
        return;
    }

    AppSlave1_TaskButton(app, now_ms);
}

// LED 패턴 진행을 slave1 정책에 위임한다.
// blink 종료 뒤 모드 복귀 같은 역할 규칙도
// 같은 흐름 안에서 자연스럽게 이어지도록 이 task에서 호출한다.
void AppCore_TaskLed(AppCore *app, uint32_t now_ms)
{
    if (app == NULL)
    {
        return;
    }

    AppSlave1_TaskLed(app, now_ms);
}

// 참고:
// OK 요청은 지금 queue가 비기를 기다리는 쪽에 가깝고,
// 실제 master 수신 성공까지 확인하지는 않는다.
// 제품 단계에서는 response 기반 추적, 재시도 간격, 포기 기준을 조금 더 명확히 두는 편이 안전하다.
