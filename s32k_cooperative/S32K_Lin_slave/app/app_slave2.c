// slave2 정책을 구현한 파일이다.
// ADC 값을 샘플링하고 최신 LIN 상태 프레임을 게시하며,
// 해석된 센서 상태에 따라 로컬 LED 표시도 갱신한다.
#include "app_slave2.h"

#include <stddef.h>
#include <stdio.h>

#include "app_core_internal.h"
#include "../runtime/runtime_io.h"

// 유효한 센서 값이 없거나 샘플 오류가 발생했을 때 사용할 fail-closed LIN status를 설정한다.
static void AppSlave2_SetFailClosedLinStatus(AppCore *app)
{
    LinStatusFrame status = { 0 };

    if ((app == NULL) || (app->lin_enabled == 0U))
    {
        return;
    }

    status.adc_value = 0U;
    status.zone = LIN_ZONE_EMERGENCY;
    status.emergency_latched = 1U;
    status.valid = 0U;
    status.fault = 1U;
    LinModule_SetSlaveStatus(&app->lin_module, &status);
}

// 최신 ADC snapshot을 표시용 문자열로 변환하여 화면 상태에 반영한다.
static void AppSlave2_UpdateAdcText(AppCore *app,
                                    InfraStatus sample_status,
                                    const AdcSnapshot *snapshot)
{
    if (app == NULL)
    {
        return;
    }

    if (sample_status == INFRA_STATUS_EMPTY)
    {
        AppCore_SetAdcText(app, "waiting");
        return;
    }

    if ((sample_status != INFRA_STATUS_OK) || (snapshot == NULL))
    {
        AppCore_SetAdcText(app, "sample fault -> fail closed");
        return;
    }

    (void)snprintf(app->adc_text,
                   sizeof(app->adc_text),
                   "%u (%s, lock=%u)",
                   (unsigned int)snapshot->raw_value,
                   AdcModule_ZoneText(snapshot->zone),
                   (unsigned int)snapshot->emergency_latched);
}

// 최신 센서 상태를 LIN slave status cache에 반영한다.
// master가 다음 status PID를 poll하면,
// 이 cache 내용이 응답 payload로 사용된다.
static void AppSlave2_PublishLinStatus(AppCore *app,
                                       InfraStatus sample_status,
                                       const AdcSnapshot *snapshot)
{
    LinStatusFrame status = { 0 };

    if ((app == NULL) || (snapshot == NULL) || (app->lin_enabled == 0U))
    {
        return;
    }

    status.adc_value = snapshot->raw_value;
    status.zone = snapshot->zone;
    status.emergency_latched = snapshot->emergency_latched;
    status.valid = (sample_status == INFRA_STATUS_OK) ? 1U : 0U;
    status.fault = (sample_status == INFRA_STATUS_OK) ? 0U : 1U;
    LinModule_SetSlaveStatus(&app->lin_module, &status);
}

// 현재 센서 zone과 latch 상태에 따라 로컬 LED 패턴을 결정한다.
static void AppSlave2_UpdateLedPattern(AppCore *app, const AdcSnapshot *snapshot)
{
    if ((app == NULL) || (snapshot == NULL) || (app->led2_enabled == 0U))
    {
        return;
    }

    if (snapshot->emergency_latched != 0U)
    {
        LedModule_SetPattern(&app->slave2_led, LED_PATTERN_RED_BLINK);
        return;
    }

    if (snapshot->zone == ADC_ZONE_SAFE)
    {
        LedModule_SetPattern(&app->slave2_led, LED_PATTERN_GREEN_SOLID);
        return;
    }

    if (snapshot->zone == ADC_ZONE_WARNING)
    {
        LedModule_SetPattern(&app->slave2_led, LED_PATTERN_YELLOW_SOLID);
        return;
    }

    LedModule_SetPattern(&app->slave2_led, LED_PATTERN_RED_SOLID);
}

// 센서 노드 역할에 필요한 모듈을 초기화한다.
// slave2는 로컬 LED 표시, ADC 샘플링,
// LIN slave 상태 제공을 모두 담당하므로 세 모듈을 함께 준비한다.
InfraStatus AppSlave2_Init(AppCore *app)
{
    LedConfig led_config;
    AdcConfig adc_config;
    LinConfig lin_config;

    if (app == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if ((RuntimeIo_GetSlave2LedConfig(&led_config) == INFRA_STATUS_OK) &&
        (LedModule_Init(&app->slave2_led, &led_config) == INFRA_STATUS_OK))
    {
        app->led2_enabled = 1U;
    }

    if ((RuntimeIo_GetSlave2AdcConfig(&adc_config) == INFRA_STATUS_OK) &&
        (AdcModule_Init(&app->adc_module, &adc_config) == INFRA_STATUS_OK))
    {
        app->adc_enabled = 1U;
    }
    else
    {
        AppCore_SetAdcText(app, "binding req");
    }

    if (RuntimeIo_GetSlaveLinConfig(&lin_config) == INFRA_STATUS_OK)
    {
        RuntimeIo_AttachLinModule(&app->lin_module);
        if (LinModule_Init(&app->lin_module, &lin_config) == INFRA_STATUS_OK)
        {
            app->lin_enabled = 1U;
            AppCore_SetLinLinkText(app, "ready");
            AppSlave2_SetFailClosedLinStatus(app);
        }
        else
        {
            AppCore_SetLinLinkText(app, "binding req");
        }
    }
    else
    {
        AppCore_SetLinLinkText(app, "binding req");
    }

    return INFRA_STATUS_OK;
}

// LIN으로 전달된 승인 token을 소비한다.
// token 수신만으로 latch를 즉시 해제하지 않고,
// zone이 더 이상 emergency가 아닐 때에만 ADC 모듈에 clear를 요청한다.
void AppSlave2_HandleLinOkToken(AppCore *app)
{
    if ((app == NULL) || (app->adc_enabled == 0U))
    {
        return;
    }

    if ((LinModule_ConsumeSlaveOkToken(&app->lin_module) != 0U) &&
        (AdcModule_ClearEmergencyLatch(&app->adc_module) == INFRA_STATUS_OK))
    {
        AppCore_SetLinInputText(app, "ok token in");
    }
}

// ADC 값을 샘플링하고 최신 해석 상태를 반영한다.
// 이 task는 센서 획득, LIN 상태 cache 갱신,
// 콘솔 문자열 업데이트와 로컬 LED 갱신을 함께 수행한다.
void AppSlave2_TaskAdc(AppCore *app, uint32_t now_ms)
{
    AdcSnapshot snapshot;
    InfraStatus sample_status;

    if ((app == NULL) || (app->adc_enabled == 0U))
    {
        return;
    }

    AdcModule_Task(&app->adc_module, now_ms);
    sample_status = AdcModule_GetSnapshot(&app->adc_module, &snapshot);
    if (sample_status == INFRA_STATUS_EMPTY)
    {
        AppSlave2_UpdateAdcText(app, sample_status, NULL);
        return;
    }

    AppSlave2_UpdateAdcText(app, sample_status, &snapshot);
    AppSlave2_PublishLinStatus(app, sample_status, &snapshot);
    AppSlave2_UpdateLedPattern(app, &snapshot);
}

// 참고:
// 현재는 ADC 텍스트 갱신, LIN 게시, LED 반영이 하나의 task에 모여 있다.
// 상태 종류가 늘어나면 상태 계산과 출력 반영을 분리하는 편이 유지보수에 유리하다.
