// ADC 샘플링과 zone 해석을 위한 공개 인터페이스다.
// app 계층은 보드 callback과 threshold, latch 관리 대신
// 하나의 의미 기반 센서 모듈만 사용하게 된다.
#ifndef ADC_MODULE_H
#define ADC_MODULE_H

#include "../core/infra_types.h"

typedef enum
{
    ADC_ZONE_SAFE = 0,
    ADC_ZONE_WARNING,
    ADC_ZONE_DANGER,
    ADC_ZONE_EMERGENCY
} AdcZone;

typedef InfraStatus (*AdcHwInitFn)(void *context);
typedef InfraStatus (*AdcHwSampleFn)(void *context, uint16_t *out_raw_value);
typedef struct AdcModule AdcModule;

// 공용 모듈에 전달되는 보드 전용 ADC 설정 구조체다.
// Runtime IO가 callback과 threshold를 채워 넣어,
// ADC 모듈이 generated peripheral symbol과 분리되도록 만든다.
typedef struct
{
    AdcHwInitFn   init_fn;
    AdcHwSampleFn sample_fn;
    void         *hw_context;
    uint32_t      sample_period_ms;
    uint16_t      range_max;
    uint16_t      safe_max;
    uint16_t      warning_max;
    uint16_t      emergency_min;
} AdcConfig;

// application 계층이 보는 최신 해석 ADC sample이다.
// 이 snapshot은 raw 값과 ADC 상태 구간, latch 상태만 담고,
// 유효성/오류 판정은 GetSnapshot 반환값으로 분리한다.
typedef struct
{
    uint16_t raw_value;
    uint8_t  zone;
    uint8_t  emergency_latched;
} AdcSnapshot;

InfraStatus AdcModule_Init(AdcModule *module, const AdcConfig *config);
void        AdcModule_Task(AdcModule *module, uint32_t now_ms);
InfraStatus AdcModule_GetSnapshot(const AdcModule *module, AdcSnapshot *out_snapshot);
InfraStatus AdcModule_ClearEmergencyLatch(AdcModule *module);
const char *AdcModule_ZoneText(uint8_t zone);

#endif
