// CAN 현장 반응 slave 내부 보조 함수 선언이다.
// slave1 정책이 모드 문자열과 CAN 보조 함수를 사용하더라도,
// 외부에는 실제 task 진입점만 보이도록 보조 함수를 분리한다.
#ifndef APP_CORE_INTERNAL_H
#define APP_CORE_INTERNAL_H

#include "app_core.h"

#include "../drivers/led_module.h"
#include "../services/can_module_internal.h"

typedef enum
{
    APP_SLAVE1_MODE_BOOT = 0,
    APP_SLAVE1_MODE_NORMAL,
    APP_SLAVE1_MODE_EMERGENCY,
    APP_SLAVE1_MODE_ACK_BLINK
} AppSlave1Mode;

typedef enum
{
    APP_SLAVE1_OK_REQUEST_IDLE = 0,
    APP_SLAVE1_OK_REQUEST_WAIT_CAN_QUEUE
} AppSlave1OkRequestState;

typedef struct
{
    uint32_t             can_response_count;
    uint32_t             can_timeout_count;
    uint32_t             can_send_fail_count;
    CanServiceResultKind last_can_result_kind;
    CanResultCode        last_can_result_code;
    uint8_t              last_can_detail_code;
    CanCommandCode       last_can_command_code;
} AppCoreDiag;

struct AppCore
{
    uint8_t                 initialized;
    uint8_t                 local_node_id;
    uint8_t                 can_enabled;
    uint8_t                 slave1_led_enabled;
    AppSlave1Mode           slave1_mode;
    AppSlave1OkRequestState slave1_ok_request_state;
    uint8_t                 button_last_sample_pressed;
    uint8_t                 button_stable_pressed;
    uint8_t                 button_same_sample_count;
    uint8_t                 can_max_submit_per_tick;
    uint32_t                can_default_timeout_ms;
    uint32_t                heartbeat_count;
    CanModule               can_module;
    LedModule               slave1_led;
    AppCoreDiag             diag;
    char                    mode_text[32];
    char                    button_text[32];
    char                    can_input_text[48];
};

void        AppCore_SetModeText(AppCore *app, const char *text);
void        AppCore_SetButtonText(AppCore *app, const char *text);
void        AppCore_SetCanInputText(AppCore *app, const char *text);
InfraStatus AppCore_RequestSlave1Ok(AppCore *app);
InfraStatus AppCore_InitCanModule(AppCore *app);

#endif
