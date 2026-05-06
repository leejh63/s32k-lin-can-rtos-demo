// master AppCore 구현 내부에서만 쓰는 보조 함수 선언이다.
// coordinator 정책이 UI 문자열과 공통 CAN 보조 함수를 사용하더라도,
// 외부에는 필요한 인터페이스만 노출되도록 범위를 줄인다.
#ifndef APP_CORE_INTERNAL_H
#define APP_CORE_INTERNAL_H

#include "app_core.h"
#include "app_ble_bridge.h"
#include "app_console_internal.h"
#include "../services/can_module.h"
#include "../services/lin_module_internal.h"

#define APP_CORE_MODE_TEXT_CAPACITY      32U
#define APP_CORE_BUTTON_TEXT_CAPACITY    32U
#define APP_CORE_ADC_TEXT_CAPACITY       48U
#define APP_CORE_CAN_INPUT_TEXT_CAPACITY 48U
#define APP_CORE_LIN_INPUT_TEXT_CAPACITY 48U
#define APP_CORE_LIN_LINK_TEXT_CAPACITY  32U

typedef enum
{
    APP_MASTER_OK_RELAY_IDLE = 0,
    APP_MASTER_OK_RELAY_WAIT_SLAVE_CLEAR
} AppMasterOkRelayState;

typedef struct
{
    uint8_t  state;
    uint8_t  retry_count;
    uint32_t started_ms;
    uint32_t last_retry_ms;
} AppMasterOkRelay;

struct AppCore
{
    uint8_t          initialized;
    uint8_t          local_node_id;
    uint8_t          console_enabled;
    uint8_t          can_enabled;
    uint8_t          lin_enabled;
    uint8_t          master_emergency_active;
    uint8_t          can_last_activity;
    uint8_t          lin_last_reported_zone;
    uint8_t          lin_last_reported_lock;
    uint8_t          lin_last_reported_fault;
    uint32_t         heartbeat_count;
    uint32_t         uart_task_count;
    uint32_t         can_task_count;
    AppMasterOkRelay ok_relay;
    AppBleBridge     ble_bridge;
    AppConsole       console;
    CanModule        can_module;
    LinModule        lin_module;
    char             mode_text[APP_CORE_MODE_TEXT_CAPACITY];
    char             button_text[APP_CORE_BUTTON_TEXT_CAPACITY];
    char             adc_text[APP_CORE_ADC_TEXT_CAPACITY];
    char             can_input_text[APP_CORE_CAN_INPUT_TEXT_CAPACITY];
    char             lin_input_text[APP_CORE_LIN_INPUT_TEXT_CAPACITY];
    char             lin_link_text[APP_CORE_LIN_LINK_TEXT_CAPACITY];
};

void        AppCore_SetModeText(AppCore *app, const char *text);
void        AppCore_SetButtonText(AppCore *app, const char *text);
void        AppCore_SetAdcText(AppCore *app, const char *text);
void        AppCore_SetCanInputText(AppCore *app, const char *text);
void        AppCore_SetLinInputText(AppCore *app, const char *text);
void        AppCore_SetLinLinkText(AppCore *app, const char *text);
void        AppCore_SetResultText(AppCore *app, const char *text);
const char *AppCore_GetLinZoneText(uint8_t zone);
uint8_t     AppCore_QueueCanCommandCode(AppCore *app,
                                        uint8_t target_node_id,
                                        uint8_t command_code,
                                        uint8_t need_response);
InfraStatus AppCore_InitConsoleCan(AppCore *app);

#endif
