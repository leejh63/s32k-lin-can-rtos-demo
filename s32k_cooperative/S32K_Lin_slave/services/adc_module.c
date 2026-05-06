// ADC 샘플링과 zone 분류를 구현한 파일이다.
// raw sample을 해석된 상태로 변환하고,
// 승인 절차에서 사용하는 emergency latch를 유지한다.
#include "adc_module_internal.h"

#include <stddef.h>
#include <string.h>

// raw ADC sample 하나를 ADC 상태 구간으로 매핑한다.
// threshold 비교 순서를 이 함수에 모아 두어,
// task 경로가 sample 처리와 latch 유지에 집중할 수 있게 한다.
static uint8_t AdcModule_ClassifyZone(const AdcConfig *config, uint16_t raw_value)
{
    if (raw_value < config->safe_max)
    {
        return ADC_ZONE_SAFE;
    }

    if (raw_value < config->warning_max)
    {
        return ADC_ZONE_WARNING;
    }

    if (raw_value < config->emergency_min)
    {
        return ADC_ZONE_DANGER;
    }

    return ADC_ZONE_EMERGENCY;
}

// 내부 sample 상태를 외부 API에서 사용하는 InfraStatus 값으로 변환한다.
static InfraStatus AdcModule_StatusFromState(AdcSampleState sample_state)
{
    switch (sample_state)
    {
        case ADC_SAMPLE_STATE_VALID:
            return INFRA_STATUS_OK;

        case ADC_SAMPLE_STATE_FAULT:
            return INFRA_STATUS_IO_ERROR;

        case ADC_SAMPLE_STATE_EMPTY:
        default:
            return INFRA_STATUS_EMPTY;
    }
}

// ADC 모듈 설정이 샘플링과 zone 분류에 필요한 최소 조건을 만족하는지 확인한다.
static uint8_t AdcModule_IsConfigValid(const AdcConfig *config)
{
    if ((config == NULL) ||
        (config->sample_fn == NULL) ||
        (config->sample_period_ms == 0U) ||
        (config->range_max == 0U))
    {
        return 0U;
    }

    if ((config->safe_max > config->warning_max) ||
        (config->warning_max > config->emergency_min) ||
        (config->emergency_min > config->range_max))
    {
        return 0U;
    }

    return 1U;
}

// 샘플 오류 시 사용할 fail-closed snapshot을 설정한다.
static void AdcModule_SetFailClosedSnapshot(AdcModule *module)
{
    if (module == NULL)
    {
        return;
    }

    module->snapshot.raw_value = 0U;
    module->snapshot.zone = ADC_ZONE_EMERGENCY;
    module->snapshot.emergency_latched = 1U;
    module->sample_state = ADC_SAMPLE_STATE_FAULT;
}

// 보드 callback과 threshold 설정으로 ADC 모듈을 초기화한다.
// 먼저 설정 유효성을 검증한 뒤,
// 필요한 하드웨어 준비를 platform binding에 요청한다.
InfraStatus AdcModule_Init(AdcModule *module, const AdcConfig *config)
{
    InfraStatus status;

    if ((module == NULL) || (config == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(module, 0, sizeof(*module));
    module->config = *config;

    if (AdcModule_IsConfigValid(&module->config) == 0U)
    {
        return INFRA_STATUS_NOT_READY;
    }

    module->sample_state = ADC_SAMPLE_STATE_EMPTY;

    if (module->config.init_fn != NULL)
    {
        status = module->config.init_fn(module->config.hw_context);
        if (status != INFRA_STATUS_OK)
        {
            return status;
        }
    }

    module->initialized = 1U;
    return INFRA_STATUS_OK;
}

// 주기적으로 ADC를 샘플링하고 현재 snapshot을 갱신한다.
// emergency latch 규칙도 이 함수에서 함께 적용하여,
// 상위 계층이 센서 상태를 일관된 방식으로 해석하도록 한다.
void AdcModule_Task(AdcModule *module, uint32_t now_ms)
{
    InfraStatus status;
    uint16_t    raw_value;
    uint16_t    range_ceiling;

    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    if (Infra_TimeIsDue(now_ms, module->last_sample_ms, module->config.sample_period_ms) == 0U)
    {
        return;
    }

    module->last_sample_ms = now_ms;
    raw_value = 0U;
    status = module->config.sample_fn(module->config.hw_context, &raw_value);
    if (status != INFRA_STATUS_OK)
    {
        AdcModule_SetFailClosedSnapshot(module);
        return;
    }

    range_ceiling = (module->config.range_max > 0U) ? (uint16_t)(module->config.range_max - 1U) : 0U;
    if (raw_value > range_ceiling)
    {
        raw_value = range_ceiling;
    }

    module->snapshot.raw_value = raw_value;
    module->snapshot.zone = AdcModule_ClassifyZone(&module->config, raw_value);
    module->sample_state = ADC_SAMPLE_STATE_VALID;

    if (module->snapshot.zone == ADC_ZONE_EMERGENCY)
    {
        module->snapshot.emergency_latched = 1U;
    }
}

// 가장 최근 snapshot을 호출자에게 복사한다.
// 상태 값도 함께 반환하여,
// 호출자가 waiting, fault, valid 상태를 동일한 API로 구분할 수 있게 한다.
InfraStatus AdcModule_GetSnapshot(const AdcModule *module, AdcSnapshot *out_snapshot)
{
    if ((module == NULL) || (out_snapshot == NULL) || (module->initialized == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    *out_snapshot = module->snapshot;
    return AdcModule_StatusFromState(module->sample_state);
}

// 승인 이후 emergency latch 해제를 시도한다.
// 최신 zone이 더 이상 emergency가 아닐 때에만 latch를 지워,
// 이른 복구를 방지한다.
InfraStatus AdcModule_ClearEmergencyLatch(AdcModule *module)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (module->sample_state == ADC_SAMPLE_STATE_EMPTY)
    {
        return INFRA_STATUS_EMPTY;
    }

    if (module->sample_state == ADC_SAMPLE_STATE_FAULT)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    if (module->snapshot.zone == ADC_ZONE_EMERGENCY)
    {
        return INFRA_STATUS_BUSY;
    }

    module->snapshot.emergency_latched = 0U;
    return INFRA_STATUS_OK;
}

// zone 값을 사람이 읽기 쉬운 짧은 문자열로 변환한다.
const char *AdcModule_ZoneText(uint8_t zone)
{
    switch (zone)
    {
        case ADC_ZONE_SAFE:
            return "safe";

        case ADC_ZONE_WARNING:
            return "warning";

        case ADC_ZONE_DANGER:
            return "danger";

        case ADC_ZONE_EMERGENCY:
            return "emergency";

        default:
            return "unknown";
    }
}

// 참고:
// 현재 latch 정책은 명확하지만, 복구 조건이 늘어나면
// zone 판단과 latch 해제 조건을 별도 보조 함수로 분리하는 편이 이해와 유지보수에 유리하다.
