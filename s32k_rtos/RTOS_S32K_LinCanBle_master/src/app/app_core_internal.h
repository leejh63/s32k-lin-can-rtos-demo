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

typedef enum
{
    APP_CORE_CAN_PRESENT_NONE = 0,
    APP_CORE_CAN_PRESENT_RESULT_TEXT = 1U << 0,
    APP_CORE_CAN_PRESENT_CAN_INPUT_TEXT = 1U << 1
} AppCoreCanPresentationMask;

typedef struct
{
    uint8_t pending_mask;
    char    result_text[APP_CONSOLE_RESULT_VIEW_SIZE];
    char    can_input_text[APP_CORE_CAN_INPUT_TEXT_CAPACITY];
} AppCoreCanPresentation;

typedef struct
{
    uint8_t pending;
    char    result_text[APP_CONSOLE_RESULT_VIEW_SIZE];
} AppCoreLinResultPresentation;

typedef struct
{
    uint8_t pending;
    char    task_text[APP_CONSOLE_TASK_VIEW_SIZE];
    char    source_text[APP_CONSOLE_SOURCE_VIEW_SIZE];
    char    value_text[APP_CONSOLE_VALUE_VIEW_SIZE];
} AppCoreRenderSnapshot;

typedef struct
{
    uint8_t  ready;
    uint8_t  uart_error;
    uint8_t  can_last_activity;
    uint32_t heartbeat_count;
    uint32_t can_task_count;
    uint32_t uart_task_count;
    char     mode_text[APP_CORE_MODE_TEXT_CAPACITY];
    char     button_text[APP_CORE_BUTTON_TEXT_CAPACITY];
    char     adc_text[APP_CORE_ADC_TEXT_CAPACITY];
    char     can_input_text[APP_CORE_CAN_INPUT_TEXT_CAPACITY];
    char     lin_input_text[APP_CORE_LIN_INPUT_TEXT_CAPACITY];
    char     lin_link_text[APP_CORE_LIN_LINK_TEXT_CAPACITY];
} AppCoreRenderInputSnapshot;

struct AppCore
{
    uint8_t          initialized;
    uint8_t          local_node_id;
    uint8_t          console_enabled;
    uint8_t          can_enabled;
    uint8_t          lin_enabled;
    uint8_t          master_emergency_active;
    uint8_t          can_tx_request_pending;
    uint8_t          can_last_activity;
    uint8_t          lin_last_reported_zone;
    uint8_t          lin_last_reported_lock;
    uint8_t          lin_last_reported_fault;
    uint8_t          master_ok_request_from_local_pending;
    uint8_t          master_ok_request_from_can_pending;
    uint32_t         heartbeat_count;
    uint32_t         uart_task_count;
    uint32_t         can_task_count;
    AppMasterOkRelay ok_relay;
    AppBleBridge     ble_bridge;
    AppConsole       console;
    CanModule        can_module;
    CanModuleRequest can_tx_request;
    AppCoreCanPresentation can_presentation;
    AppCoreLinResultPresentation lin_result_presentation;
    AppCoreRenderInputSnapshot render_input_snapshot;
    AppCoreRenderSnapshot  render_snapshot;
    LinModule        lin_module;
    char             mode_text[APP_CORE_MODE_TEXT_CAPACITY];
    char             button_text[APP_CORE_BUTTON_TEXT_CAPACITY];
    char             adc_text[APP_CORE_ADC_TEXT_CAPACITY];
    char             can_input_text[APP_CORE_CAN_INPUT_TEXT_CAPACITY];
    char             lin_input_text[APP_CORE_LIN_INPUT_TEXT_CAPACITY];
    char             lin_link_text[APP_CORE_LIN_LINK_TEXT_CAPACITY];
};

// AppCore mode 문자열을 갱신한다.
void        AppCore_SetModeText(AppCore *app, const char *text);
// AppCore button 문자열을 갱신한다.
void        AppCore_SetButtonText(AppCore *app, const char *text);
// AppCore ADC 문자열을 갱신한다.
void        AppCore_SetAdcText(AppCore *app, const char *text);
// AppCore CAN 입력 문자열을 갱신한다.
void        AppCore_SetCanInputText(AppCore *app, const char *text);
// AppCore LIN 입력 문자열을 갱신한다.
void        AppCore_SetLinInputText(AppCore *app, const char *text);
// AppCore LIN 링크 문자열을 갱신한다.
void        AppCore_SetLinLinkText(AppCore *app, const char *text);
// AppCore 결과 영역 문자열을 갱신한다.
void        AppCore_SetResultText(AppCore *app, const char *text);
// AppCore CAN presentation 요청을 pending mailbox에 적재한다.
void        AppCore_QueueCanPresentation(AppCore *app, uint8_t kind, const char *text);
// dispatcher owner가 CAN presentation 요청을 소비한다.
uint8_t     AppCore_ConsumeCanPresentation(AppCore *app, AppCoreCanPresentation *out_presentation);
// LIN owner가 만든 result text를 UART commit owner 쪽으로 적재한다.
void        AppCore_QueueLinResultText(AppCore *app, const char *text);
// UART owner가 LIN result text를 소비한다.
uint8_t     AppCore_ConsumeLinResultText(AppCore *app,
                                         AppCoreLinResultPresentation *out_presentation);
// render task가 만든 latest snapshot을 pending mailbox에 적재한다.
void        AppCore_QueueRenderSnapshot(AppCore *app,
                                        const char *task_text,
                                        const char *source_text,
                                        const char *value_text);
// render task가 owner들이 갱신한 latest render input image를 원자적으로 복사한다.
uint8_t     AppCore_CopyRenderInputSnapshot(AppCore *app, AppCoreRenderInputSnapshot *out_snapshot);
// uart owner가 render snapshot을 소비한다.
uint8_t     AppCore_ConsumeRenderSnapshot(AppCore *app, AppCoreRenderSnapshot *out_snapshot);
// AppCore outgoing CAN 요청을 pending mailbox에 적재한다.
uint8_t     AppCore_QueueCanTxRequest(AppCore *app, const CanModuleRequest *request);
// AppCore outgoing CAN 요청 mailbox를 소비한다.
uint8_t     AppCore_ConsumeCanTxRequest(AppCore *app, CanModuleRequest *out_request);
// CAN incoming이 남긴 master OK policy 입력을 pending으로 적재한다.
void        AppCore_QueueMasterOkRequestFromCan(AppCore *app);
// UART owner가 감지한 local master OK policy 입력을 pending으로 적재한다.
void        AppCore_QueueMasterOkRequestFromLocal(AppCore *app);
// lin_poll owner가 local master OK policy 입력을 소비한다.
uint8_t     AppCore_ConsumeMasterOkRequestFromLocal(AppCore *app);
// lin_poll owner가 CAN-originated master OK policy 입력을 소비한다.
uint8_t     AppCore_ConsumeMasterOkRequestFromCan(AppCore *app);
const char *AppCore_GetLinZoneText(uint8_t zone);
uint8_t     AppCore_QueueCanCommandCode(AppCore *app,
                                        uint8_t target_node_id,
                                        uint8_t command_code,
                                        uint8_t need_response);
InfraStatus AppCore_InitConsoleCan(AppCore *app);

#endif
