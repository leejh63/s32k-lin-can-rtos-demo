// ADC hardware binding 구현 파일이다.
// 상위 ADC 모듈은 sample 함수 포인터만 보고,
// 실제 SDK context와 드라이버 초기화는 이 파일이 대신 맡는다.
#include "adc_hw.h"

#include <stddef.h>
#include <string.h>

#include "../platform/s32k_sdk/isosdk_adc.h"

static IsoSdkAdcContext s_adc_hw_context;

// 현재 보드/빌드에서 ADC를 사용할 수 있는지 알려준다.
uint8_t AdcHw_IsSupported(void)
{
    return IsoSdk_AdcIsSupported();
}

// ADC 하드웨어 context를 초기화한다.
InfraStatus AdcHw_Init(void *context)
{
    (void)context;
    (void)memset(&s_adc_hw_context, 0, sizeof(s_adc_hw_context));
    return (IsoSdk_AdcInit(&s_adc_hw_context) != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// ADC raw sample 하나를 읽어 온다.
InfraStatus AdcHw_Sample(void *context, uint16_t *out_raw_value)
{
    (void)context;
    return (IsoSdk_AdcSample(&s_adc_hw_context, out_raw_value) != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// 참고:
// 현재는 단일 정적 ADC context 전제라 단순하지만,
// 채널이 늘어나면 context와 sample 함수를 분리하는 쪽이 더 자연스럽다.
