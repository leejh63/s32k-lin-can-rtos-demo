// slave2 정책을 구현한 파일이다.
// ADC 값을 샘플링하고 최신 LIN 상태 프레임을 게시하며,
// 해석된 센서 상태에 따라 로컬 LED 표시도 갱신한다.
#include "app_slave2.h"

#include <stddef.h>
#include <stdio.h>

#include "app_core_internal.h"
#include "../runtime/runtime_io.h"

// 0..input_max 값을 0..output_max duty로 선형 변환한다.
static uint16_t AppSlave2_ScaleToDuty(uint16_t value,
                                      uint16_t input_max,
                                      uint16_t output_max)
{
    uint32_t scaled_value;

    if ((input_max == 0U) || (output_max == 0U))
    {
        return 0U;
    }

    if (value >= input_max)
    {
        return output_max;
    }

    scaled_value = ((uint32_t)value * (uint32_t)output_max) / (uint32_t)input_max;
    return (uint16_t)scaled_value;
}

// raw ADC 값을 non-emergency 구간의 연속 LED 색으로 바꾼다.
// 낮을 때는 green, safe 끝에서는 yellow, emergency 직전에는 red가 되게 맞춘다.
static void AppSlave2_MapAdcToGradientDuty(uint16_t raw_value,
                                           uint16_t max_duty,
                                           uint16_t *out_red_duty,
                                           uint16_t *out_green_duty)
{
    uint16_t safe_max;
    uint16_t emergency_min;
    uint16_t descending_span;
    uint16_t distance_to_emergency;

    if ((out_red_duty == NULL) || (out_green_duty == NULL))
    {
        return;
    }

    safe_max = RuntimeIo_GetSlave2AdcSafeMax();
    emergency_min = RuntimeIo_GetSlave2AdcEmergencyMin();

    if (raw_value <= safe_max)
    {
        *out_red_duty = AppSlave2_ScaleToDuty(raw_value, safe_max, max_duty);
        *out_green_duty = max_duty;
        return;
    }

    if (raw_value >= emergency_min)
    {
        *out_red_duty = max_duty;
        *out_green_duty = 0U;
        return;
    }

    descending_span = (uint16_t)(emergency_min - safe_max);
    distance_to_emergency = (uint16_t)(emergency_min - raw_value);
    *out_red_duty = max_duty;
    *out_green_duty = AppSlave2_ScaleToDuty(distance_to_emergency,
                                            descending_span,
                                            max_duty);
}

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

// 현재 센서 상태에 따라 로컬 LED를 갱신한다.
// emergency latch는 red blink를 유지하고,
// 그 외 구간은 ADC 값에 따라 green -> yellow -> red gradient를 만든다.
static void AppSlave2_UpdateLedOutput(AppCore *app, const AdcSnapshot *snapshot)
{
    uint16_t red_duty;
    uint16_t green_duty;
    uint16_t max_duty;

    if ((app == NULL) || (snapshot == NULL) || (app->led2_enabled == 0U))
    {
        return;
    }

    if (snapshot->emergency_latched != 0U)
    {
        // 같은 blink 패턴을 주기마다 다시 설정하면 위상이 계속 초기화되어,
        // 실제 점멸이 불규칙하게 보일 수 있으므로 상태가 바뀔 때만 적용한다.
        if (LedModule_GetPattern(&app->slave2_led) != LED_PATTERN_RED_BLINK)
        {
            LedModule_SetPattern(&app->slave2_led, LED_PATTERN_RED_BLINK);
        }

        return;
    }

    max_duty = app->slave2_led.config.on_duty;
    AppSlave2_MapAdcToGradientDuty(snapshot->raw_value,
                                   max_duty,
                                   &red_duty,
                                   &green_duty);
    LedModule_SetCustomSolid(&app->slave2_led, red_duty, green_duty);
}

// 센서 노드 역할에 필요한 모듈을 초기화한다.
// slave2는 로컬 LED 표시, ADC 샘플링,
// LIN slave 상태 제공을 모두 담당하므로 세 모듈을 함께 준비한다.
// 주의: 현재 구현은 일부 모듈이 실패해도 가능한 기능만 켜고 INFRA_STATUS_OK를 반환한다.
// bring-up 단계에서는 편하지만, 추후 필수 모듈 실패를 강하게 드러내고 싶다면 반환 정책을 더 엄격히 나눌 수 있다.
InfraStatus AppSlave2_Init(AppCore *app)
{
    LedConfig led_config;
    AdcConfig adc_config;
    LinConfig lin_config;

    if (app == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    // 1) 로컬 상태 표시용 LED를 먼저 준비한다.
    if ((RuntimeIo_GetSlave2LedConfig(&led_config) == INFRA_STATUS_OK) &&
        (LedModule_Init(&app->slave2_led, &led_config) == INFRA_STATUS_OK))
    {
        app->led2_enabled = 1U;
    }

    // 2) ADC 샘플링 모듈을 준비한다. 실패 시 텍스트를 binding req로 남겨 현장 확인 힌트를 준다.
    if ((RuntimeIo_GetSlave2AdcConfig(&adc_config) == INFRA_STATUS_OK) &&
        (AdcModule_Init(&app->adc_module, &adc_config) == INFRA_STATUS_OK))
    {
        app->adc_enabled = 1U;
    }
    else
    {
        AppCore_SetAdcText(app, "binding req");
    }

    // 3) LIN slave 상태기계를 올리고, 최초 status는 fail-closed 값으로 채운다.
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
// 주의: 지금은 "상태 계산 + 게시 + 표시"가 한 함수에 모여 있어 읽기는 쉽지만 책임은 다소 넓다.
// 상태 종류가 더 늘어나면 계산 단계와 출력 반영 단계를 나누는 편이 자연스럽다.
void AppSlave2_TaskAdc(AppCore *app, uint32_t now_ms)
{
    AdcSnapshot snapshot;
    InfraStatus sample_status;

    if ((app == NULL) || (app->adc_enabled == 0U))
    {
        return;
    }

    // 1) 주기 조건이 맞으면 ADC 모듈이 snapshot을 갱신한다.
    AdcModule_Task(&app->adc_module, now_ms);

    // 2) 가장 최근 snapshot과 그 상태(waiting / fault / valid)를 가져온다.
    sample_status = AdcModule_GetSnapshot(&app->adc_module, &snapshot);
    if (sample_status == INFRA_STATUS_EMPTY)
    {
        AppSlave2_UpdateAdcText(app, sample_status, NULL);
        return;
    }

    // 3) 동일한 snapshot을 기준으로 텍스트, LIN cache, LED 출력을 같은 방향으로 맞춘다.
    AppSlave2_UpdateAdcText(app, sample_status, &snapshot);
    AppSlave2_PublishLinStatus(app, sample_status, &snapshot);
    AppSlave2_UpdateLedOutput(app, &snapshot);
}

// 참고:
// 현재는 ADC 텍스트 갱신, LIN 게시, LED 반영이 하나의 task에 모여 있다.
// 상태 종류가 늘어나면 상태 계산과 출력 반영을 분리하는 편이 유지보수에 유리하다.
