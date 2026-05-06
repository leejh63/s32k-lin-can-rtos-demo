// master 측 주요 제어 흐름을 구성하는 파일이다.
// console, CAN, LIN 경로를 연결하고,
// master 동작에 필요한 공통 상태와 표시 문자열을 관리한다.
#include "app_core.h"

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

// 현재 시스템 모드 표시 문자열을 갱신한다.
void AppCore_SetModeText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->mode_text, sizeof(app->mode_text), text);
    }
}

// 승인 버튼 관련 상태 문자열을 갱신한다.
void AppCore_SetButtonText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->button_text, sizeof(app->button_text), text);
    }
}

// ADC 상태 표시 문자열을 갱신한다.
void AppCore_SetAdcText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->adc_text, sizeof(app->adc_text), text);
    }
}

// CAN 입력 상태 표시 문자열을 갱신한다.
void AppCore_SetCanInputText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->can_input_text, sizeof(app->can_input_text), text);
    }
}

// LIN 입력 상태 표시 문자열을 갱신한다.
void AppCore_SetLinInputText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->lin_input_text, sizeof(app->lin_input_text), text);
    }
}

// LIN 링크 상태 표시 문자열을 갱신한다.
void AppCore_SetLinLinkText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->lin_link_text, sizeof(app->lin_link_text), text);
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
    if ((app == NULL) || (app->can_enabled == 0U))
    {
        return 0U;
    }

    return (CanModule_QueueCommand(&app->can_module,
                                   target_node_id,
                                   command_code,
                                   0U,
                                   0U,
                                   need_response) == INFRA_STATUS_OK) ? 1U : 0U;
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
static void AppCore_HandleCanIncoming(AppCore *app,
                                      const CanMessage *message,
                                      uint32_t now_ms)
{
    char    buffer[APP_CONSOLE_RESULT_VIEW_SIZE];
    uint8_t response_code;

    if ((app == NULL) || (message == NULL))
    {
        return;
    }

    if (message->message_type == CAN_MSG_EVENT)
    {
        (void)snprintf(buffer,
                       sizeof(buffer),
                       "[event] code=%u from=%u arg0=%u arg1=%u",
                       (unsigned int)message->payload[0],
                       (unsigned int)message->source_node_id,
                       (unsigned int)message->payload[1],
                       (unsigned int)message->payload[2]);
        AppCore_SetResultText(app, buffer);
        return;
    }

    if (message->message_type == CAN_MSG_TEXT)
    {
        (void)snprintf(buffer,
                       sizeof(buffer),
                       "[text] from=%u %s",
                       (unsigned int)message->source_node_id,
                       message->text);
        AppCore_SetResultText(app, buffer);
        return;
    }

    if (message->message_type != CAN_MSG_COMMAND)
    {
        return;
    }

    response_code = CAN_RES_NOT_SUPPORTED;
    AppMaster_HandleCanCommand(app, message, now_ms, &response_code);

    (void)snprintf(buffer,
                   sizeof(buffer),
                   "[remote] cmd=%u from=%u",
                   (unsigned int)message->payload[0],
                   (unsigned int)message->source_node_id);
    AppCore_SetResultText(app, buffer);

    if ((message->flags & CAN_MSG_FLAG_NEED_RESPONSE) != 0U)
    {
        (void)CanModule_QueueResponse(&app->can_module,
                                      message->source_node_id,
                                      message->request_id,
                                      response_code,
                                      0U);
    }
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
    }
}

// 콘솔 UART 입력과 출력 진행을 한 번 처리한다.
void AppCore_TaskUart(AppCore *app, uint32_t now_ms)
{
    if ((app == NULL) || (app->console_enabled == 0U))
    {
        return;
    }

    app->uart_task_count++;
    AppConsole_Task(&app->console, now_ms);
    AppBleBridge_Task(&app->ble_bridge, &app->console, now_ms);
}

// 콘솔에서 꺼낸 CAN 요청을 제출하고 결과와 수신 메시지를 소비한다.
// master 입장에서는 operator 입력, 원격 응답, 원격 요청이 모두 이 task에서 만난다.
void AppCore_TaskCan(AppCore *app, uint32_t now_ms)
{
    AppConsoleCanCommand command;
    CanServiceResult     result;
    CanMessage           message;
    char                 buffer[APP_CONSOLE_RESULT_VIEW_SIZE];
    uint8_t              activity;
    uint8_t              command_count;
    uint8_t              result_count;
    uint8_t              incoming_count;
    uint8_t              command_code;

    if ((app == NULL) || (app->can_enabled == 0U))
    {
        return;
    }

    activity = 0U;
    command_count = 0U;
    while ((command_count < APP_CAN_MAX_CONSOLE_COMMANDS_PER_TICK) &&
           (app->console_enabled != 0U) &&
           (AppConsole_TryPopCanCommand(&app->console, &command) != 0U))
    {
        command_count++;
        switch (command.type)
        {
            case APP_CONSOLE_CAN_CMD_OPEN:
            case APP_CONSOLE_CAN_CMD_CLOSE:
            case APP_CONSOLE_CAN_CMD_OFF:
            case APP_CONSOLE_CAN_CMD_TEST:
                if (AppCore_MapConsoleCanCommandType(command.type, &command_code) != 0U)
                {
                    (void)CanModule_QueueCommand(&app->can_module,
                                                 command.target_node_id,
                                                 command_code,
                                                 0U,
                                                 0U,
                                                 1U);
                    activity = 1U;
                }
                break;

            case APP_CONSOLE_CAN_CMD_TEXT:
                (void)CanModule_QueueText(&app->can_module,
                                          command.target_node_id,
                                          command.text);
                activity = 1U;
                break;

            case APP_CONSOLE_CAN_CMD_EVENT:
                (void)CanModule_QueueEvent(&app->can_module,
                                           command.target_node_id,
                                           command.event_code,
                                           command.arg0,
                                           command.arg1);
                activity = 1U;
                break;

            default:
                break;
        }
    }

    CanModule_Task(&app->can_module, now_ms);
    result_count = 0U;
    while ((result_count < APP_CAN_MAX_RESULTS_PER_TICK) &&
           (CanModule_TryPopResult(&app->can_module, &result) != 0U))
    {
        result_count++;
        AppCore_FormatCanResult(&result, buffer, sizeof(buffer));
        AppCore_SetResultText(app, buffer);
        activity = 1U;
    }

    incoming_count = 0U;
    while ((incoming_count < APP_CAN_MAX_INCOMING_PER_TICK) &&
           (CanModule_TryPopIncoming(&app->can_module, &message) != 0U))
    {
        incoming_count++;
        AppCore_HandleCanIncoming(app, &message, now_ms);
        activity = 1U;
    }

    app->can_last_activity = activity;
    if (activity != 0U)
    {
        app->can_task_count++;
    }
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

// 주기적인 LIN poll과 로컬 OK 입력 소비를 묶어 처리한다.
// operator가 직접 낸 승인 요청도 결국 이 task 주기를 따라,
// 일반 LIN 상태기계 흐름 안에서 소화되게 만든다.
void AppCore_TaskLinPoll(AppCore *app, uint32_t now_ms)
{
    if (app == NULL)
    {
        return;
    }

    if (app->console_enabled != 0U)
    {
        if (AppConsole_ConsumeLocalOk(&app->console) != 0U)
        {
            AppMaster_RequestOk(app, now_ms);
        }
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

    can_text = (app->can_last_activity != 0U) ? "ok" : "idle";
    uart_text = (AppConsole_IsError(&app->console) == 0U) ? "ok" : "error";

    (void)snprintf(task_text,
                   sizeof(task_text),
                   "HeartBeat : alive / %lu\r\n"
                   "CAN       : %s / %lu\r\n"
                   "LIN       : %s\r\n"
                   "UART      : %s / %lu",
                   (unsigned long)app->heartbeat_count,
                   can_text,
                   (unsigned long)app->can_task_count,
                   app->lin_link_text,
                   uart_text,
                   (unsigned long)app->uart_task_count);

    (void)snprintf(source_text,
                   sizeof(source_text),
                   "from [can] \"%s\"\r\n"
                   "from [lin] \"%s\"",
                   app->can_input_text,
                   app->lin_input_text);

    (void)snprintf(value_text,
                   sizeof(value_text),
                   "Mode   : %s\r\n"
                   "Button : %s\r\n"
                   "ADC    : %s",
                   app->mode_text,
                   app->button_text,
                   app->adc_text);

    AppConsole_SetTaskText(&app->console, task_text);
    AppConsole_SetSourceText(&app->console, source_text);
    AppConsole_SetValueText(&app->console, value_text);
    AppConsole_Render(&app->console);
    AppBleBridge_Sync(&app->ble_bridge, &app->console);
}

// 참고:
// CAN 요청 enqueue 실패를 현재는 일부 화면 문구로만 흘려 보내는 경향이 있어서,
// 확장 시에는 실패 원인을 조금 더 분명히 남기면 디버깅이 수월해진다.
