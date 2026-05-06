// LIN sensor slave 쪽 주요 흐름을 묶는 구현 파일이다.
// slave2에 필요한 ADC, LIN, LED task만 남겨,
// 센서 상태 생성과 latch 해제 흐름을 간단하게 추적할 수 있게 한다.
#include "app_core.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_core_internal.h"
#include "app_slave2.h"
#include "../runtime/runtime_io.h"

// 작은 상태 문자열 버퍼 하나를 안전하게 덮어쓴다.
static void AppCore_SetText(char *buffer, size_t size, const char *text)
{
    if ((buffer == NULL) || (size == 0U) || (text == NULL))
    {
        return;
    }

    (void)snprintf(buffer, size, "%s", text);
}

// ADC 상태 표시 문자열을 갱신한다.
void AppCore_SetAdcText(AppCore *app, const char *text)
{
    if (app != NULL)
    {
        AppCore_SetText(app->adc_text, sizeof(app->adc_text), text);
    }
}

// LIN 입력 상태 문자열을 갱신한다.
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

// 초기 화면에 표시할 기본 문구를 설정한다.
static void AppCore_InitDefaultTexts(AppCore *app)
{
    if (app == NULL)
    {
        return;
    }

    AppCore_SetAdcText(app, "waiting");
    AppCore_SetLinInputText(app, "ready");
    AppCore_SetLinLinkText(app, "waiting");
}

// sensor slave용 AppCore 전체 상태를 초기화한다.
// 기본 텍스트를 먼저 맞춘 뒤,
// 역할별 모듈 준비가 끝나면 정상 실행 상태로 넘어간다.
InfraStatus AppCore_Init(AppCore *app)
{
    InfraStatus status;

    if (app == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(app, 0, sizeof(*app));
    app->local_node_id = RuntimeIo_GetLocalNodeId();
    AppCore_InitDefaultTexts(app);

    status = AppSlave2_Init(app);
    if (status != INFRA_STATUS_OK)
    {
        return status;
    }

    app->initialized = 1U;
    return INFRA_STATUS_OK;
}

// tick ISR에서 LIN timeout service 쪽으로 신호를 넘긴다.
// 여기서는 아주 짧은 연결만 맡고,
// 실제 상태 전개는 task 문맥에서 계속 이어 간다.
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

// callback에서 적재해 둔 LIN event를 빠르게 처리하고,
// ok token이 들어왔는지도 즉시 확인한다.
// 주의: 여기서 무거운 정책 계산을 늘리기보다, 현재처럼 "event 소비 + token 확인" 정도로 유지하는 것이 적절하다.
void AppCore_TaskLinFast(AppCore *app, uint32_t now_ms)
{
    if ((app == NULL) || (app->lin_enabled == 0U))
    {
        return;
    }

    // 1) ISR/callback이 남긴 event bit를 task 문맥에서 소비한다.
    LinModule_TaskFast(&app->lin_module, now_ms);

    // 2) slave가 방금 받은 OK token이 있으면 latch 해제를 시도한다.
    AppSlave2_HandleLinOkToken(app);
}

// 로컬 LED pattern 상태를 한 단계 진행시킨다.
void AppCore_TaskLed(AppCore *app, uint32_t now_ms)
{
    if ((app == NULL) || (app->led2_enabled == 0U))
    {
        return;
    }

    LedModule_Task(&app->slave2_led, now_ms);
}

// 센서 샘플링과 상태 게시 흐름을 진행한다.
void AppCore_TaskAdc(AppCore *app, uint32_t now_ms)
{
    if ((app == NULL) || (app->adc_enabled == 0U))
    {
        return;
    }

    AppSlave2_TaskAdc(app, now_ms);
}

// 참고:
// slave 쪽 AppCore는 단순해서 가독성이 높지만, 앞으로 표시 문자열이 더 늘어나면
// 상태 갱신 보조 함수와 UI용 텍스트 생성을 조금 더 나누는 편이 정리가 잘 된다.
