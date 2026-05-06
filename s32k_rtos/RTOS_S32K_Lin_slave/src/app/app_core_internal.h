// LIN sensor slave AppCore 내부 보조 함수 선언이다.
// 센서 노드는 console 없이도 상태 문자열을 내부에 유지하므로,
// 필요한 텍스트 갱신 함수만 좁게 노출한다.
#ifndef APP_CORE_INTERNAL_H
#define APP_CORE_INTERNAL_H

#include "app_core.h"
#include "../drivers/led_module.h"
#include "../services/adc_module_internal.h"
#include "../services/lin_module_internal.h"

#define APP_CORE_ADC_TEXT_CAPACITY       48U
#define APP_CORE_LIN_INPUT_TEXT_CAPACITY 48U
#define APP_CORE_LIN_LINK_TEXT_CAPACITY  32U

struct AppCore
{
    uint8_t   initialized;
    uint8_t   local_node_id;
    uint8_t   lin_enabled;
    uint8_t   adc_enabled;
    uint8_t   led2_enabled;
    uint32_t  heartbeat_count;
    LinModule lin_module;
    LedModule slave2_led;
    AdcModule adc_module;
    char      adc_text[APP_CORE_ADC_TEXT_CAPACITY];
    char      lin_input_text[APP_CORE_LIN_INPUT_TEXT_CAPACITY];
    char      lin_link_text[APP_CORE_LIN_LINK_TEXT_CAPACITY];
};

void AppCore_SetAdcText(AppCore *app, const char *text);
void AppCore_SetLinInputText(AppCore *app, const char *text);
void AppCore_SetLinLinkText(AppCore *app, const char *text);

#endif
