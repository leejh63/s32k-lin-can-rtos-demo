// master 측 주요 제어 흐름을 구성하는 파일이다.
// console, CAN, LIN 경로를 연결하고,
// master 동작에 필요한 공통 상태와 표시 문자열을 관리한다.
#include "app_core.h"

#include "FreeRTOS.h"
#include "task.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_core_internal.h"
#include "app_master.h"
#include "../runtime/runtime_io.h"

// 짧은 UI 텍스트 버퍼를 안전하게 갱신한다.
// 여러 view 필드가 동일한 복사 규칙을 사용하므로,
// 문자열 갱신 방식을 이 보조 함수로 통일한다.
static void AppCore_SetText(char *buffer, size_t size, const char *text)
{
    if ((buffer == NULL) || (size == 0U) || (text == NULL))
    {
        return;
    }

    (void)snprintf(buffer, size, "%s", text);
}

// mailbox shared field 접근은 짧은 critical section으로만 감싼다.
static void AppCore_EnterMailboxCritical(void)
{
    taskENTER_CRITICAL();
}

static void AppCore_ExitMailboxCritical(void)
{
    taskEXIT_CRITICAL();
}

// mailbox 보호 구간 안에서는 formatting 대신 bounded copy만 수행한다.
static void AppCore_CopyTextBounded(char *buffer, size_t size, const char *text)
{
    size_t copy_length;

    if ((buffer == NULL) || (size == 0U) || (text == NULL))
    {
        return;
    }

    copy_length = strlen(text);
    if (copy_length >= size)
    {
        copy_length = size - 1U;
    }

    (void)memcpy(buffer, text, copy_length);
    buffer[copy_length] = '\0';
}

// 여러 owner가 만드는 render 입력을 shared latest image에 모은다.
static void AppCore_UpdateRenderInputSnapshotText(AppCore *app,
                                                  char *snapshot_buffer,
                                                  size_t snapshot_size,
                                                  const char *text)
{
    if ((app == NULL) || (snapshot_buffer == NULL) || (snapshot_size == 0U) || (text == NULL))
    {
        return;
    }

    AppCore_EnterMailboxCritical();
    AppCore_CopyTextBounded(snapshot_buffer, snapshot_size, text);
    app->render_input_snapshot.ready = 1U;
    AppCore_ExitMailboxCritical();
}

static void AppCore_UpdateRenderInputSnapshotHeartbeat(AppCore *app, uint32_t heartbeat_count)
{
    if (app == NULL)
    {
        return;
    }

    AppCore_EnterMailboxCritical();
    app->render_input_snapshot.heartbeat_count = heartbeat_count;
    app->render_input_snapshot.ready = 1U;
    AppCore_ExitMailboxCritical();
}

static void AppCore_UpdateRenderInputSnapshotCanState(AppCore *app,
                                                      uint8_t can_last_activity,
                                                      uint32_t can_task_count)
{
    if (app == NULL)
    {
        return;
    }

    AppCore_EnterMailboxCritical();
    app->render_input_snapshot.can_last_activity = can_last_activity;
    app->render_input_snapshot.can_task_count = can_task_count;
    app->render_input_snapshot.ready = 1U;
    AppCore_ExitMailboxCritical();
}

static void AppCore_UpdateRenderInputSnapshotUartState(AppCore *app,
                                                       uint8_t uart_error,
                                                       uint32_t uart_task_count)
{
    if (app == NULL)
    {
        return;
    }

    AppCore_EnterMailboxCritical();
    app->render_input_snapshot.uart_error = uart_error;
    app->render_input_snapshot.uart_task_count = uart_task_count;
    app->render_input_snapshot.ready = 1U;
    AppCore_ExitMailboxCritical();
}

// CAN TX mailbox의 pending flag 조회도 보호 구간에서 수행한다.
static uint8_t AppCore_HasPendingCanTxRequest(const AppCore *app)
{
    uint8_t pending;

    if (app == NULL)
    {
        return 0U;
    }

    AppCore_EnterMailboxCritical();
    pending = app->can_tx_request_pending;
    AppCore_ExitMailboxCritical();
    return pending;
}

// 현재 시스템 모드 표시 문자열을 갱신한다.
void AppCore_SetModeText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->mode_text, sizeof(app->mode_text), text);
        AppCore_UpdateRenderInputSnapshotText(app,
                                              app->render_input_snapshot.mode_text,
                                              sizeof(app->render_input_snapshot.mode_text),
                                              text);
    }
}

// 승인 버튼 관련 상태 문자열을 갱신한다.
void AppCore_SetButtonText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->button_text, sizeof(app->button_text), text);
        AppCore_UpdateRenderInputSnapshotText(app,
                                              app->render_input_snapshot.button_text,
                                              sizeof(app->render_input_snapshot.button_text),
                                              text);
    }
}

// ADC 상태 표시 문자열을 갱신한다.
void AppCore_SetAdcText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->adc_text, sizeof(app->adc_text), text);
        AppCore_UpdateRenderInputSnapshotText(app,
                                              app->render_input_snapshot.adc_text,
                                              sizeof(app->render_input_snapshot.adc_text),
                                              text);
    }
}

// CAN 입력 상태 표시 문자열을 갱신한다.
void AppCore_SetCanInputText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->can_input_text, sizeof(app->can_input_text), text);
        AppCore_UpdateRenderInputSnapshotText(app,
                                              app->render_input_snapshot.can_input_text,
                                              sizeof(app->render_input_snapshot.can_input_text),
                                              text);
    }
}

// LIN 입력 상태 표시 문자열을 갱신한다.
void AppCore_SetLinInputText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->lin_input_text, sizeof(app->lin_input_text), text);
        AppCore_UpdateRenderInputSnapshotText(app,
                                              app->render_input_snapshot.lin_input_text,
                                              sizeof(app->render_input_snapshot.lin_input_text),
                                              text);
    }
}

// LIN 링크 상태 표시 문자열을 갱신한다.
void AppCore_SetLinLinkText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->lin_link_text, sizeof(app->lin_link_text), text);
        AppCore_UpdateRenderInputSnapshotText(app,
                                              app->render_input_snapshot.lin_link_text,
                                              sizeof(app->render_input_snapshot.lin_link_text),
                                              text);
    }
}

// 콘솔 결과 영역에 표시할 문자열을 갱신한다.
void AppCore_SetResultText(AppCore *app, const char *text)
{
    if ((app != NULL) && (app->console_enabled != 0U))
    {
        AppConsole_SetResultText(&app->console, text);
    }
}

// CAN task는 operator view를 직접 건드리지 않고,
// presentation mailbox에 latest value만 적재한다.
void AppCore_QueueCanPresentation(AppCore *app, uint8_t kind, const char *text)
{
    if ((app == NULL) || (text == NULL))
    {
        return;
    }

    AppCore_EnterMailboxCritical();

    if ((kind & APP_CORE_CAN_PRESENT_RESULT_TEXT) != 0U)
    {
        AppCore_CopyTextBounded(app->can_presentation.result_text,
                                sizeof(app->can_presentation.result_text),
                                text);
        app->can_presentation.pending_mask |= APP_CORE_CAN_PRESENT_RESULT_TEXT;
    }

    if ((kind & APP_CORE_CAN_PRESENT_CAN_INPUT_TEXT) != 0U)
    {
        AppCore_CopyTextBounded(app->can_presentation.can_input_text,
                                sizeof(app->can_presentation.can_input_text),
                                text);
        app->can_presentation.pending_mask |= APP_CORE_CAN_PRESENT_CAN_INPUT_TEXT;
    }

    AppCore_ExitMailboxCritical();
}

// UART owner가 한 번에 latest CAN presentation만 가져가고 mailbox를 비운다.
uint8_t AppCore_ConsumeCanPresentation(AppCore *app, AppCoreCanPresentation *out_presentation)
{
    uint8_t consumed;

    if ((app == NULL) || (out_presentation == NULL))
    {
        return 0U;
    }

    consumed = 0U;
    AppCore_EnterMailboxCritical();
    if (app->can_presentation.pending_mask != 0U)
    {
        *out_presentation = app->can_presentation;
        (void)memset(&app->can_presentation, 0, sizeof(app->can_presentation));
        consumed = 1U;
    }
    AppCore_ExitMailboxCritical();
    return consumed;
}

// LIN owner는 result text를 직접 console에 commit하지 않고,
// UART owner가 추후 반영할 single-slot mailbox에 latest 값만 남긴다.
void AppCore_QueueLinResultText(AppCore *app, const char *text)
{
    if ((app == NULL) || (text == NULL))
    {
        return;
    }

    AppCore_EnterMailboxCritical();
    AppCore_CopyTextBounded(app->lin_result_presentation.result_text,
                            sizeof(app->lin_result_presentation.result_text),
                            text);
    app->lin_result_presentation.pending = 1U;
    AppCore_ExitMailboxCritical();
}

// UART owner가 LIN result latest value를 가져가고 mailbox를 비운다.
uint8_t AppCore_ConsumeLinResultText(AppCore *app,
                                     AppCoreLinResultPresentation *out_presentation)
{
    uint8_t consumed;

    if ((app == NULL) || (out_presentation == NULL))
    {
        return 0U;
    }

    consumed = 0U;
    AppCore_EnterMailboxCritical();
    if (app->lin_result_presentation.pending != 0U)
    {
        *out_presentation = app->lin_result_presentation;
        (void)memset(&app->lin_result_presentation, 0, sizeof(app->lin_result_presentation));
        consumed = 1U;
    }
    AppCore_ExitMailboxCritical();
    return consumed;
}

// task/source/value snapshot은 single-slot mailbox로 넘기고,
// 실제 console/ble commit은 UART owner가 맡는다.
void AppCore_QueueRenderSnapshot(AppCore *app,
                                 const char *task_text,
                                 const char *source_text,
                                 const char *value_text)
{
    if ((app == NULL) || (task_text == NULL) || (source_text == NULL) || (value_text == NULL))
    {
        return;
    }

    AppCore_EnterMailboxCritical();
    AppCore_CopyTextBounded(app->render_snapshot.task_text,
                            sizeof(app->render_snapshot.task_text),
                            task_text);
    AppCore_CopyTextBounded(app->render_snapshot.source_text,
                            sizeof(app->render_snapshot.source_text),
                            source_text);
    AppCore_CopyTextBounded(app->render_snapshot.value_text,
                            sizeof(app->render_snapshot.value_text),
                            value_text);
    app->render_snapshot.pending = 1U;
    AppCore_ExitMailboxCritical();
}

// render는 plain field를 직접 보지 않고 owner들이 만든 latest image만 원자적으로 복사한다.
uint8_t AppCore_CopyRenderInputSnapshot(AppCore *app, AppCoreRenderInputSnapshot *out_snapshot)
{
    uint8_t copied;

    if ((app == NULL) || (out_snapshot == NULL))
    {
        return 0U;
    }

    copied = 0U;
    AppCore_EnterMailboxCritical();
    if (app->render_input_snapshot.ready != 0U)
    {
        *out_snapshot = app->render_input_snapshot;
        copied = 1U;
    }
    AppCore_ExitMailboxCritical();
    return copied;
}

uint8_t AppCore_ConsumeRenderSnapshot(AppCore *app, AppCoreRenderSnapshot *out_snapshot)
{
    uint8_t consumed;

    if ((app == NULL) || (out_snapshot == NULL))
    {
        return 0U;
    }

    consumed = 0U;
    AppCore_EnterMailboxCritical();
    if (app->render_snapshot.pending != 0U)
    {
        *out_snapshot = app->render_snapshot;
        (void)memset(&app->render_snapshot, 0, sizeof(app->render_snapshot));
        consumed = 1U;
    }
    AppCore_ExitMailboxCritical();
    return consumed;
}

// outgoing CAN submit owner를 AppCore_TaskCan() 한 곳으로 모으기 위해,
// 다른 경로들은 먼저 이 single-slot mailbox에 요청만 적재한다.
uint8_t AppCore_QueueCanTxRequest(AppCore *app, const CanModuleRequest *request)
{
    uint8_t queued;

    if ((app == NULL) || (request == NULL))
    {
        return 0U;
    }

    queued = 0U;
    AppCore_EnterMailboxCritical();
    if (app->can_tx_request_pending == 0U)
    {
        app->can_tx_request = *request;
        app->can_tx_request_pending = 1U;
        queued = 1U;
    }
    AppCore_ExitMailboxCritical();
    return queued;
}

// dispatcher 기준선에서는 AppCore_TaskCan()이 outgoing CAN submit owner다.
// 그래서 다른 경로가 만든 요청도 이 mailbox로만 넘겨 받아 소비한다.
uint8_t AppCore_ConsumeCanTxRequest(AppCore *app, CanModuleRequest *out_request)
{
    uint8_t consumed;

    if ((app == NULL) || (out_request == NULL))
    {
        return 0U;
    }

    consumed = 0U;
    AppCore_EnterMailboxCritical();
    if (app->can_tx_request_pending != 0U)
    {
        *out_request = app->can_tx_request;
        (void)memset(&app->can_tx_request, 0, sizeof(app->can_tx_request));
        app->can_tx_request_pending = 0U;
        consumed = 1U;
    }
    AppCore_ExitMailboxCritical();
    return consumed;
}

// CAN 수신 경로가 곧바로 LIN owner를 건드리지 않도록,
// 승인 요청 입력은 작은 pending mailbox에 먼저 적재한다.
void AppCore_QueueMasterOkRequestFromCan(AppCore *app)
{
    if (app != NULL)
    {
        AppCore_EnterMailboxCritical();
        app->master_ok_request_from_can_pending = 1U;
        AppCore_ExitMailboxCritical();
    }
}

// 현재 구조에서는 LIN owner가 policy input owner다.
// 그래서 CAN 쪽 pending 입력도 이 함수로 한 번에 소비한다.
uint8_t AppCore_ConsumeMasterOkRequestFromCan(AppCore *app)
{
    uint8_t pending;

    if (app == NULL)
    {
        return 0U;
    }

    AppCore_EnterMailboxCritical();
    pending = app->master_ok_request_from_can_pending;
    app->master_ok_request_from_can_pending = 0U;
    AppCore_ExitMailboxCritical();
    return pending;
}

// UART owner만 console mutable state를 직접 읽고,
// local OK는 이후 LIN policy owner가 보도록 pending으로만 넘긴다.
void AppCore_QueueMasterOkRequestFromLocal(AppCore *app)
{
    if (app != NULL)
    {
        AppCore_EnterMailboxCritical();
        app->master_ok_request_from_local_pending = 1U;
        AppCore_ExitMailboxCritical();
    }
}

uint8_t AppCore_ConsumeMasterOkRequestFromLocal(AppCore *app)
{
    uint8_t pending;

    if (app == NULL)
    {
        return 0U;
    }

    AppCore_EnterMailboxCritical();
    pending = app->master_ok_request_from_local_pending;
    app->master_ok_request_from_local_pending = 0U;
    AppCore_ExitMailboxCritical();
    return pending;
}

// LIN zone 값을 사람이 읽기 쉬운 짧은 문자열로 변환한다.
const char *AppCore_GetLinZoneText(uint8_t zone)
{
    switch (zone)
    {
        case LIN_ZONE_SAFE:
            return "safe";

        case LIN_ZONE_WARNING:
            return "warning";

        case LIN_ZONE_DANGER:
            return "danger";

        case LIN_ZONE_EMERGENCY:
            return "emergency";

        default:
            return "unknown";
    }
}

// 초기 화면에 표시할 기본 문구를 설정한다.
static void AppCore_InitDefaultTexts(AppCore *app)
{
    if (app == NULL)
    {
        return;
    }

    AppCore_SetModeText(app, "normal");
    AppCore_SetButtonText(app, "waiting");
    AppCore_SetAdcText(app, "waiting");
    AppCore_SetCanInputText(app, "waiting");
    AppCore_SetLinInputText(app, "waiting");
    AppCore_SetLinLinkText(app, "waiting");
}

// command code만 포함하는 단순 CAN 요청을 큐에 적재한다.
// master 정책에서 짧은 명령을 자주 사용하므로,
// 공통 호출 형태를 별도 보조 함수로 분리하였다.
uint8_t AppCore_QueueCanCommandCode(AppCore *app,
                                    uint8_t target_node_id,
                                    uint8_t command_code,
                                    uint8_t need_response)
{
    CanModuleRequest request;

    if ((app == NULL) || (app->can_enabled == 0U))
    {
        return 0U;
    }

    (void)memset(&request, 0, sizeof(request));
    request.kind = CAN_MODULE_REQUEST_COMMAND;
    request.target_node_id = target_node_id;
    request.code0 = command_code;
    request.code1 = 0U;
    request.code2 = 0U;
    request.need_response = need_response;

    return AppCore_QueueCanTxRequest(app, &request);
}

// master가 쓰는 콘솔과 CAN 경로를 함께 초기화한다.
// operator 입력과 원격 명령 통신이 바로 이어지도록,
// 두 경로를 한 단계에서 함께 초기화한다.
InfraStatus AppCore_InitConsoleCan(AppCore *app)
{
    CanModuleConfig can_config;

    if (app == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (AppConsole_Init(&app->console, app->local_node_id) == INFRA_STATUS_OK)
    {
        app->console_enabled = 1U;
        AppCore_UpdateRenderInputSnapshotUartState(app,
                                                   AppConsole_IsError(&app->console),
                                                   app->uart_task_count);
        (void)AppBleBridge_Init(&app->ble_bridge);
    }

    (void)memset(&can_config, 0, sizeof(can_config));
    can_config.local_node_id = app->local_node_id;
    can_config.default_timeout_ms = 300U;
    can_config.max_submit_per_tick = 2U;
    if (CanModule_Init(&app->can_module, &can_config) != INFRA_STATUS_OK)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    app->can_enabled = 1U;
    return INFRA_STATUS_OK;
}

// CAN service 결과를 콘솔 표시용 짧은 문장으로 변환한다.
static void AppCore_FormatCanResult(const CanServiceResult *result, char *buffer, size_t size)
{
    const char *name;

    if ((result == NULL) || (buffer == NULL) || (size == 0U))
    {
        return;
    }

    switch (result->command_code)
    {
        case CAN_CMD_OPEN:
            name = "open";
            break;

        case CAN_CMD_CLOSE:
            name = "close";
            break;

        case CAN_CMD_OFF:
            name = "off";
            break;

        case CAN_CMD_TEST:
            name = "test";
            break;

        case CAN_CMD_OK:
            name = "ok";
            break;

        case CAN_CMD_EMERGENCY:
            name = "emergency";
            break;

        default:
            name = "unknown";
            break;
    }

    if (result->kind == CAN_SERVICE_RESULT_TIMEOUT)
    {
        (void)snprintf(buffer,
                       size,
                       "[timeout] %s target=%u",
                       name,
                       (unsigned int)result->source_node_id);
        return;
    }

    if (result->result_code == CAN_RES_OK)
    {
        (void)snprintf(buffer,
                       size,
                       "[ok] %s target=%u",
                       name,
                       (unsigned int)result->source_node_id);
        return;
    }

    if (result->result_code == CAN_RES_NOT_SUPPORTED)
    {
        (void)snprintf(buffer,
                       size,
                       "[error] %s not supported target=%u",
                       name,
                       (unsigned int)result->source_node_id);
        return;
    }

    (void)snprintf(buffer,
                   size,
                   "[error] %s target=%u code=%u",
                   name,
                   (unsigned int)result->source_node_id,
                   (unsigned int)result->result_code);
}

// 새로 수신한 CAN 메시지를 종류별로 해석하여 master 정책으로 전달한다.
// event와 text는 즉시 UI에 반영하고,
// command는 필요하면 응답까지 만들어 다시 queue에 넣는다.
// NOTE:
// 현재는 event/text/command를 이 함수 하나에서 갈라서 처리한다.
// message type이 늘어나면 dispatch table 형태로 바꾸는 편이 더 안전하다.
static void AppCore_HandleCanIncoming(AppCore *app,
                                      const CanMessage *message)
{
    char    buffer[APP_CONSOLE_RESULT_VIEW_SIZE];
    uint8_t response_code;

    if ((app == NULL) || (message == NULL))
    {
        return;
    }

    // 1) operator가 바로 봐야 하는 event/text는 즉시 result 영역에 반영한다.
    if (message->message_type == CAN_MSG_EVENT)
    {
        (void)snprintf(buffer,
                       sizeof(buffer),
                       "[event] code=%u from=%u arg0=%u arg1=%u",
                       (unsigned int)message->payload[0],
                       (unsigned int)message->source_node_id,
                       (unsigned int)message->payload[1],
                       (unsigned int)message->payload[2]);
        AppCore_QueueCanPresentation(app, APP_CORE_CAN_PRESENT_RESULT_TEXT, buffer);
        return;
    }

    if (message->message_type == CAN_MSG_TEXT)
    {
        (void)snprintf(buffer,
                       sizeof(buffer),
                       "[text] from=%u %s",
                       (unsigned int)message->source_node_id,
                       message->text);
        AppCore_QueueCanPresentation(app, APP_CORE_CAN_PRESENT_RESULT_TEXT, buffer);
        return;
    }

    if (message->message_type != CAN_MSG_COMMAND)
    {
        return;
    }

    // 2) command는 직접 LIN owner를 건드리지 않고,
    // policy input pending만 적재한 뒤 필요 시 protocol response를 되돌린다.
    response_code = CAN_RES_NOT_SUPPORTED;
    AppMaster_HandleCanCommand(app, message, &response_code);

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "[remote] cmd=%u from=%u",
                   (unsigned int)message->payload[0],
                   (unsigned int)message->source_node_id);
    AppCore_QueueCanPresentation(app, APP_CORE_CAN_PRESENT_RESULT_TEXT, buffer);

    if ((message->flags & CAN_MSG_FLAG_NEED_RESPONSE) != 0U)
    {
        (void)CanModule_QueueResponse(&app->can_module,
                                      message->source_node_id,
                                      message->request_id,
                                      response_code,
                                      0U);
    }
}

// 콘솔 명령 enum을 CAN protocol command code로 명시적으로 변환한다.
// 두 enum의 숫자 값이 우연히 같다는 전제에 의존하지 않도록 한다.
static uint8_t AppCore_MapConsoleCanCommandType(uint8_t console_type, uint8_t *out_command_code)
{
    if (out_command_code == NULL)
    {
        return 0U;
    }

    switch (console_type)
    {
        case APP_CONSOLE_CAN_CMD_OPEN:
            *out_command_code = CAN_CMD_OPEN;
            return 1U;

        case APP_CONSOLE_CAN_CMD_CLOSE:
            *out_command_code = CAN_CMD_CLOSE;
            return 1U;

        case APP_CONSOLE_CAN_CMD_OFF:
            *out_command_code = CAN_CMD_OFF;
            return 1U;

        case APP_CONSOLE_CAN_CMD_TEST:
            *out_command_code = CAN_CMD_TEST;
            return 1U;

        default:
            break;
    }

    return 0U;
}

// console/UART owner가 만든 중립 command를 AppCore outgoing CAN request로 바꾼다.
// 실제 submit은 아직 AppCore_TaskCan() 한 곳에서만 수행한다.
static uint8_t AppCore_BuildCanTxRequestFromConsoleCommand(const AppConsoleCanCommand *command,
                                                           CanModuleRequest *out_request)
{
    uint8_t command_code;

    if ((command == NULL) || (out_request == NULL))
    {
        return 0U;
    }

    (void)memset(out_request, 0, sizeof(*out_request));
    out_request->target_node_id = command->target_node_id;

    switch (command->type)
    {
        case APP_CONSOLE_CAN_CMD_OPEN:
        case APP_CONSOLE_CAN_CMD_CLOSE:
        case APP_CONSOLE_CAN_CMD_OFF:
        case APP_CONSOLE_CAN_CMD_TEST:
            if (AppCore_MapConsoleCanCommandType(command->type, &command_code) == 0U)
            {
                return 0U;
            }

            out_request->kind = CAN_MODULE_REQUEST_COMMAND;
            out_request->code0 = command_code;
            out_request->code1 = 0U;
            out_request->code2 = 0U;
            out_request->need_response = 1U;
            return 1U;

        case APP_CONSOLE_CAN_CMD_TEXT:
            out_request->kind = CAN_MODULE_REQUEST_TEXT;
            (void)snprintf(out_request->text, sizeof(out_request->text), "%s", command->text);
            return 1U;

        case APP_CONSOLE_CAN_CMD_EVENT:
            out_request->kind = CAN_MODULE_REQUEST_EVENT;
            out_request->code0 = command->event_code;
            out_request->code1 = command->arg0;
            out_request->code2 = command->arg1;
            return 1U;

        default:
            break;
    }

    return 0U;
}

// master용 AppCore 전체 상태를 초기화한다.
// 기본 텍스트와 역할별 모듈 준비를 끝낸 뒤,
// 성공했을 때만 정상 실행 상태로 전환한다.
InfraStatus AppCore_Init(AppCore *app)
{
    InfraStatus status;

    if (app == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(app, 0, sizeof(*app));
    app->local_node_id = RuntimeIo_GetLocalNodeId();
    app->lin_last_reported_zone = 0xFFU;
    app->lin_last_reported_lock = 0xFFU;
    app->lin_last_reported_fault = 0xFFU;
    AppCore_InitDefaultTexts(app);

    status = AppMaster_Init(app);
    if (status != INFRA_STATUS_OK)
    {
        return status;
    }

    app->initialized = 1U;
    return INFRA_STATUS_OK;
}

// tick ISR에서 LIN timeout service 쪽으로 신호를 넘긴다.
// 여기서는 아주 짧은 연결만 맡고,
// 실제 LIN 상태 전개는 task 문맥에서 계속 이어간다.
void AppCore_OnTickIsr(void *context)
{
    AppCore *app;

    app = (AppCore *)context;
    if ((app == NULL) || (app->lin_enabled == 0U))
    {
        return;
    }

    LinModule_OnBaseTick(&app->lin_module);
}

// 살아 있음을 나타내는 간단한 heartbeat 카운터를 증가시킨다.
void AppCore_TaskHeartbeat(AppCore *app, uint32_t now_ms)
{
    (void)now_ms;

    if (app != NULL)
    {
        app->heartbeat_count++;
        AppCore_UpdateRenderInputSnapshotHeartbeat(app, app->heartbeat_count);
    }
}

// 콘솔 UART 입력과 출력 진행을 한 번 처리한다.
void AppCore_TaskUart(AppCore *app, uint32_t now_ms)
{
    AppConsoleCanCommand command;
    CanModuleRequest     request;
    AppCoreCanPresentation can_presentation;
    AppCoreLinResultPresentation lin_presentation;
    AppCoreRenderSnapshot snapshot;

    if ((app == NULL) || (app->console_enabled == 0U))
    {
        return;
    }

    app->uart_task_count++;
    AppConsole_Task(&app->console, now_ms);
    AppBleBridge_Task(&app->ble_bridge, &app->console, now_ms);

    if (AppConsole_ConsumeLocalOk(&app->console) != 0U)
    {
        AppCore_QueueMasterOkRequestFromLocal(app);
    }

    // console queue의 owner는 계속 UART 쪽에 두고,
    // CAN task에는 명시적 mailbox로만 요청을 넘긴다.
    if ((AppCore_HasPendingCanTxRequest(app) == 0U) &&
        (AppConsole_TryPopCanCommand(&app->console, &command) != 0U) &&
        (AppCore_BuildCanTxRequestFromConsoleCommand(&command, &request) != 0U))
    {
        (void)AppCore_QueueCanTxRequest(app, &request);
    }

    AppCore_UpdateRenderInputSnapshotUartState(app,
                                               AppConsole_IsError(&app->console),
                                               app->uart_task_count);

    // UART owner가 CAN/LIN presentation의 최종 console commit을 맡는다.
    if (AppCore_ConsumeCanPresentation(app, &can_presentation) != 0U)
    {
        if ((can_presentation.pending_mask & APP_CORE_CAN_PRESENT_CAN_INPUT_TEXT) != 0U)
        {
            AppCore_SetCanInputText(app, can_presentation.can_input_text);
        }

        if ((can_presentation.pending_mask & APP_CORE_CAN_PRESENT_RESULT_TEXT) != 0U)
        {
            AppCore_SetResultText(app, can_presentation.result_text);
        }
    }

    if (AppCore_ConsumeLinResultText(app, &lin_presentation) != 0U)
    {
        AppCore_SetResultText(app, lin_presentation.result_text);
    }

    if (AppCore_ConsumeRenderSnapshot(app, &snapshot) != 0U)
    {
        AppConsole_SetTaskText(&app->console, snapshot.task_text);
        AppConsole_SetSourceText(&app->console, snapshot.source_text);
        AppConsole_SetValueText(&app->console, snapshot.value_text);
    }

    AppConsole_Render(&app->console);
    AppBleBridge_Sync(&app->ble_bridge, &app->console);
}

// 콘솔에서 꺼낸 CAN 요청을 제출하고 결과와 수신 메시지를 소비한다.
// master 입장에서는 operator 입력, 원격 응답, 원격 요청이 모두 이 task에서 만난다.
// NOTE:
// 현재 함수가 맡는 폭이 넓다. 입력 dequeue, CAN submit, result 소비, incoming dispatch를 모두 수행한다.
// 지금은 흐름이 한곳에 모여 읽기 쉽지만, 더 커지면 submit/result/incoming 단계 분리 후보가 된다.
void AppCore_TaskCan(AppCore *app, uint32_t now_ms)
{
    CanModuleRequest     request;
    CanServiceResult     result;
    CanMessage           message;
    char                 buffer[APP_CONSOLE_RESULT_VIEW_SIZE];
    uint8_t              activity;
    uint8_t              result_count;
    uint8_t              incoming_count;

    if ((app == NULL) || (app->can_enabled == 0U))
    {
        return;
    }

    activity = 0U;

    // 1) 다른 owner가 적재한 outgoing CAN 요청을 한 번 소비해 실제 submit 한다.
    if (AppCore_ConsumeCanTxRequest(app, &request) != 0U)
    {
        switch (request.kind)
        {
            case CAN_MODULE_REQUEST_COMMAND:
                (void)CanModule_QueueCommand(&app->can_module,
                                             request.target_node_id,
                                             request.code0,
                                             request.code1,
                                             request.code2,
                                             request.need_response);
                activity = 1U;
                break;

            case CAN_MODULE_REQUEST_TEXT:
                (void)CanModule_QueueText(&app->can_module,
                                          request.target_node_id,
                                          request.text);
                activity = 1U;
                break;

            case CAN_MODULE_REQUEST_EVENT:
                (void)CanModule_QueueEvent(&app->can_module,
                                           request.target_node_id,
                                           request.code0,
                                           request.code1,
                                           request.code2);
                activity = 1U;
                break;

            default:
                break;
        }
    }

    // 2) CAN transport/service를 한 번 진행시켜 TX/RX/timeout을 갱신한다.
    CanModule_Task(&app->can_module, now_ms);

    // 3) 완료된 request 결과를 UI용 문자열로 소비한다.
    result_count = 0U;
    while ((result_count < APP_CAN_MAX_RESULTS_PER_TICK) &&
           (CanModule_TryPopResult(&app->can_module, &result) != 0U))
    {
        result_count++;
        AppCore_FormatCanResult(&result, buffer, sizeof(buffer));
        AppCore_QueueCanPresentation(app, APP_CORE_CAN_PRESENT_RESULT_TEXT, buffer);
        activity = 1U;
    }

    // 4) 새로 들어온 수신 메시지를 정책 계층으로 넘긴다.
    incoming_count = 0U;
    while ((incoming_count < APP_CAN_MAX_INCOMING_PER_TICK) &&
           (CanModule_TryPopIncoming(&app->can_module, &message) != 0U))
    {
        incoming_count++;
        AppCore_HandleCanIncoming(app, &message);
        activity = 1U;
    }

    // 5) 이번 tick에 실제 통신 활동이 있었는지 기록한다.
    app->can_last_activity = activity;
    if (activity != 0U)
    {
        app->can_task_count++;
    }
    AppCore_UpdateRenderInputSnapshotCanState(app,
                                              app->can_last_activity,
                                              app->can_task_count);
}

// callback에서 적재해 둔 LIN event를 빠르게 처리한다.
// 최신 상태가 생기면 바로 master policy로 넘기고,
// 오랫동안 새 상태가 없을 때는 링크 문구도 함께 갱신한다.
void AppCore_TaskLinFast(AppCore *app, uint32_t now_ms)
{
    LinStatusFrame status;
    InfraStatus    status_ready;

    if ((app == NULL) || (app->lin_enabled == 0U))
    {
        return;
    }

    LinModule_TaskFast(&app->lin_module, now_ms);
    if (LinModule_ConsumeFreshStatus(&app->lin_module, &status) != 0U)
    {
        AppMaster_HandleFreshLinStatus(app, &status);
        return;
    }

    if (app->ok_relay.state != APP_MASTER_OK_RELAY_IDLE)
    {
        return;
    }

    status_ready = LinModule_GetLatestStatusIfFresh(&app->lin_module,
                                                    now_ms,
                                                    APP_LIN_STATUS_MAX_AGE_MS,
                                                    &status);
    if (status_ready == INFRA_STATUS_TIMEOUT)
    {
        AppCore_SetLinLinkText(app, "stale");
    }
}

// 주기적인 LIN poll과 승인 요청 mailbox 소비를 묶어 처리한다.
// operator가 local/CAN 경로로 낸 승인 요청도 결국 이 task 주기를 따라,
// 일반 LIN 상태기계 흐름 안에서 소화되게 만든다.
void AppCore_TaskLinPoll(AppCore *app, uint32_t now_ms)
{
    uint8_t ok_request_pending;

    if (app == NULL)
    {
        return;
    }

    ok_request_pending = 0U;

    if (AppCore_ConsumeMasterOkRequestFromLocal(app) != 0U)
    {
        ok_request_pending = 1U;
    }

    if (AppCore_ConsumeMasterOkRequestFromCan(app) != 0U)
    {
        ok_request_pending = 1U;
    }

    if (ok_request_pending != 0U)
    {
        AppMaster_RequestOk(app, now_ms);
    }

    if (app->lin_enabled != 0U)
    {
        LinModule_TaskPoll(&app->lin_module, now_ms);
    }

    AppMaster_AfterLinPoll(app, now_ms);
}

// 현재 AppCore 상태를 콘솔 view 문자열로 정리해 렌더링한다.
// 각 기능의 내부 구조를 그대로 노출하기보다,
// operator가 보기 좋은 요약 형태로 묶는 역할을 한다.
void AppCore_TaskRender(AppCore *app, uint32_t now_ms)
{
    AppCoreRenderInputSnapshot render_input;
    char        task_text[APP_CONSOLE_TASK_VIEW_SIZE];
    char        source_text[APP_CONSOLE_SOURCE_VIEW_SIZE];
    char        value_text[APP_CONSOLE_VALUE_VIEW_SIZE];
    const char *can_text;
    const char *uart_text;

    (void)now_ms;

    if ((app == NULL) || (app->console_enabled == 0U))
    {
        return;
    }

    if (AppCore_CopyRenderInputSnapshot(app, &render_input) == 0U)
    {
        return;
    }

    can_text = (render_input.can_last_activity != 0U) ? "ok" : "idle";
    uart_text = (render_input.uart_error == 0U) ? "ok" : "error";

    (void)snprintf(task_text,
                   sizeof(task_text),
                   "HeartBeat : alive / %lu\r\n"
                   "CAN       : %s / %lu\r\n"
                   "LIN       : %s\r\n"
                   "UART      : %s / %lu",
                   (unsigned long)render_input.heartbeat_count,
                   can_text,
                   (unsigned long)render_input.can_task_count,
                   render_input.lin_link_text,
                   uart_text,
                   (unsigned long)render_input.uart_task_count);

    (void)snprintf(source_text,
                   sizeof(source_text),
                   "from [can] \"%s\"\r\n"
                   "from [lin] \"%s\"",
                   render_input.can_input_text,
                   render_input.lin_input_text);

    (void)snprintf(value_text,
                   sizeof(value_text),
                   "Mode   : %s\r\n"
                   "Button : %s\r\n"
                   "ADC    : %s",
                   render_input.mode_text,
                   render_input.button_text,
                   render_input.adc_text);

    AppCore_QueueRenderSnapshot(app, task_text, source_text, value_text);
}

// 참고:
// CAN 요청 enqueue 실패를 현재는 일부 화면 문구로만 흘려 보내는 경향이 있어서,
// 확장 시에는 실패 원인을 조금 더 분명히 남기면 디버깅이 수월해진다.
