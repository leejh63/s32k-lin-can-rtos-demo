// slave1 policy 구현 파일이다.
// CAN 명령을 LED 동작으로 바꾸고,
// 안정된 버튼 입력을 master로 보내는 승인 요청으로 변환한다.
#include "app_slave1.h"

#include <stddef.h>
#include <stdio.h>

#include "app_config.h"
#include "app_core_internal.h"
#include "app_port.h"

// 반응 노드 역할을 초기화한다.
// slave1은 CAN 경로와 로컬 LED 제어기,
// 그리고 버튼 입력 포트가 준비되어야 정상 동작한다.
InfraStatus AppSlave1_Init(AppCore *app)
{
    LedConfig led_config;

    if (app == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (AppCore_InitCanModule(app) != INFRA_STATUS_OK)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    if (AppPort_GetSlave1LedConfig(&led_config) != INFRA_STATUS_OK)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    if (LedModule_Init(&app->slave1_led, &led_config) != INFRA_STATUS_OK)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    app->slave1_led_enabled = 1U;
    app->slave1_mode = APP_SLAVE1_MODE_NORMAL;
    app->slave1_ok_request_state = APP_SLAVE1_OK_REQUEST_IDLE;
    AppCore_SetModeText(app, "normal");
    return INFRA_STATUS_OK;
}

// CAN 제어 명령을 로컬 slave1 동작으로 바꾼다.
// master는 이 경로를 통해,
// slave1을 emergency 상태로 두거나 승인 완료 표시, 출력 해제를 지시한다.
// 주의: OPEN/CLOSE/TEST는 현재 상태 문자열 갱신 중심으로 처리하며,
// 실제 물리 동작이 연결된 상태는 아니다.
void AppSlave1_HandleCanCommand(AppCore *app,
                                const CanMessage *message,
                                uint8_t *out_response_code)
{
    char buffer[48];

    if ((app == NULL) || (message == NULL) || (out_response_code == NULL) || (app->slave1_led_enabled == 0U))
    {
        return;
    }

    // payload[0] 하나로 동작 종류를 구분한다.
    switch (message->payload[0])
    {
        case CAN_CMD_EMERGENCY:
            (void)snprintf(buffer, sizeof(buffer), "emergency in (%u)", (unsigned int)message->source_node_id);
            AppCore_EnterCritical();
            app->slave1_mode = APP_SLAVE1_MODE_EMERGENCY;
            app->slave1_ok_request_state = APP_SLAVE1_OK_REQUEST_IDLE;
            LedModule_SetPattern(&app->slave1_led, LED_PATTERN_RED_SOLID);
            AppCore_SetModeText(app, "emergency");
            AppCore_SetButtonText(app, "press ok");
            AppCore_SetCanInputText(app, buffer);
            AppCore_ExitCritical();
            *out_response_code = CAN_RES_OK;
            break;

        case CAN_CMD_OK:
            (void)snprintf(buffer, sizeof(buffer), "ok in (%u)", (unsigned int)message->source_node_id);
            AppCore_EnterCritical();
            app->slave1_mode = APP_SLAVE1_MODE_ACK_BLINK;
            app->slave1_ok_request_state = APP_SLAVE1_OK_REQUEST_IDLE;
            LedModule_StartGreenAckBlink(&app->slave1_led, APP_SLAVE1_ACK_TOGGLES);
            AppCore_SetModeText(app, "ack blink");
            AppCore_SetButtonText(app, "approved");
            AppCore_SetCanInputText(app, buffer);
            AppCore_ExitCritical();
            *out_response_code = CAN_RES_OK;
            break;

        case CAN_CMD_OFF:
            AppCore_EnterCritical();
            app->slave1_mode = APP_SLAVE1_MODE_NORMAL;
            app->slave1_ok_request_state = APP_SLAVE1_OK_REQUEST_IDLE;
            LedModule_SetPattern(&app->slave1_led, LED_PATTERN_OFF);
            AppCore_SetModeText(app, "normal");
            AppCore_SetButtonText(app, "waiting");
            AppCore_SetCanInputText(app, "waiting");
            AppCore_ExitCritical();
            *out_response_code = CAN_RES_OK;
            break;

        case CAN_CMD_OPEN:
        case CAN_CMD_CLOSE:
        case CAN_CMD_TEST:
            // 현재는 명령 흔적만 남기고 성공 응답을 돌려준다.
            // 실제 actuator 동작이 붙는다면 이 분기부터 분리하는 편이 자연스럽다.
            (void)snprintf(buffer,
                           sizeof(buffer),
                           "cmd %u in (%u)",
                           (unsigned int)message->payload[0],
                           (unsigned int)message->source_node_id);
            AppCore_SetCanInputText(app, buffer);
            *out_response_code = CAN_RES_OK;
            break;

        default:
            break;
    }
}

// 로컬 승인 버튼을 debounce하고 안정된 입력만 보고한다.
// slave1은 master가 만든 emergency 상태에서 버튼이 눌릴 때만,
// OK 요청을 예약한다.
// 주의: 여기서 예약된 요청은 이후 AppCore CAN task가 queue에 실어 보내며,
// 현재 구조상 master 응답까지 추적하는 흐름은 아니다.
void AppSlave1_TaskButton(AppCore *app, uint32_t now_ms)
{
    InfraStatus request_status;
    uint8_t raw_pressed;

    (void)now_ms;

    if ((app == NULL) || (app->can_enabled == 0U))
    {
        return;
    }

    // 1) raw 입력을 읽고 이전 샘플과 같은지 확인한다.
    raw_pressed = AppPort_ReadSlave1ButtonPressed();
    if (raw_pressed == app->button_last_sample_pressed)
    {
        if (app->button_same_sample_count < 3U)
        {
            app->button_same_sample_count++;
        }
    }
    else
    {
        app->button_same_sample_count = 0U;
        app->button_last_sample_pressed = raw_pressed;
        return;
    }

    // 2) 안정 샘플 수가 충분히 쌓였을 때만 stable state를 갱신한다.
    if (app->button_same_sample_count < 2U)
    {
        return;
    }

    if (app->button_stable_pressed == raw_pressed)
    {
        return;
    }

    app->button_stable_pressed = raw_pressed;
    if (app->button_stable_pressed == 0U)
    {
        return;
    }

    // 3) emergency 모드에서 눌림이 안정화되면 local OK 요청을 예약한다.
    request_status = AppCore_RequestSlave1Ok(app);
    if (request_status == INFRA_STATUS_NOT_READY)
    {
        return;
    }

    AppCore_SetButtonText(app, "pressed");
    if (request_status == INFRA_STATUS_OK)
    {
        AppCore_SetCanInputText(app, "ok req pending");
    }
    else
    {
        AppCore_SetCanInputText(app, "ok req busy");
    }
}

// 로컬 LED pattern 상태기계를 진행시킨다.
// 유한한 acknowledgement blink 시퀀스를 여기서 끝까지 수행하여,
// slave1이 이후 정상 standby 상태로 돌아가게 한다.
void AppSlave1_TaskLed(AppCore *app, uint32_t now_ms)
{
    if ((app == NULL) || (app->slave1_led_enabled == 0U))
    {
        return;
    }

    // LED 모듈이 내부 blink state를 진행한다.
    AppCore_EnterCritical();
    LedModule_Task(&app->slave1_led, now_ms);
    if ((app->slave1_mode == APP_SLAVE1_MODE_ACK_BLINK) &&
        (LedModule_GetPattern(&app->slave1_led) == LED_PATTERN_OFF))
    {
        app->slave1_mode = APP_SLAVE1_MODE_NORMAL;
        AppCore_SetModeText(app, "normal");
        AppCore_SetButtonText(app, "waiting");
    }
    AppCore_ExitCritical();
}
