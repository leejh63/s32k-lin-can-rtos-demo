// ADC generated driver를 얇게 감싼 구현 파일이다.
// 상위 계층은 채널 설정 구조체를 직접 다루지 않고,
// 지원 여부와 sample API만 보면 되게 정리한다.
#include "isosdk_adc.h"

#include <stddef.h>
#include <string.h>

#include "isosdk_sdk_bindings.h"

#ifdef ISOSDK_SDK_HAS_ADC

#define ISOSDK_ADC_GROUP  0U

static adc_chan_config_t s_iso_sdk_adc_chan_config;

// 현재 보드 설정에서 ADC를 쓸 수 있는지 알려준다.
uint8_t IsoSdk_AdcIsSupported(void)
{
    return 1U;
}

// ADC converter를 초기화하고 context를 사용 가능 상태로 만든다.
// 채널 설정은 이후 sample 호출 때 재사용할 수 있게,
// 시작 단계에서 한 번 정리해 둔다.
uint8_t IsoSdk_AdcInit(IsoSdkAdcContext *context)
{
    if (context == NULL)
    {
        return 0U;
    }

    (void)memset(context, 0, sizeof(*context));
    s_iso_sdk_adc_chan_config = ISOSDK_SDK_ADC_CHANNEL_CONFIG;
    s_iso_sdk_adc_chan_config.interruptEnable = false;

    ADC_DRV_ConfigConverter(ISOSDK_SDK_ADC_INSTANCE, &ISOSDK_SDK_ADC_CONVERTER_CONFIG);
    ADC_DRV_AutoCalibration(ISOSDK_SDK_ADC_INSTANCE);
    context->initialized = 1U;
    return 1U;
}

// ADC 한 번 샘플링을 수행해 12-bit 범위 값으로 반환한다.
// 상위 계층은 driver 호출 순서를 알 필요가 없고,
// 유효 범위를 넘는 값은 여기서 한 번 더 정리한다.
uint8_t IsoSdk_AdcSample(IsoSdkAdcContext *context, uint16_t *out_raw_value)
{
    uint16_t raw_value;

    if ((context == NULL) || (context->initialized == 0U) || (out_raw_value == NULL))
    {
        return 0U;
    }

    raw_value = 0U;
    ADC_DRV_ConfigChan(ISOSDK_SDK_ADC_INSTANCE, ISOSDK_ADC_GROUP, &s_iso_sdk_adc_chan_config);
    ADC_DRV_WaitConvDone(ISOSDK_SDK_ADC_INSTANCE);
    ADC_DRV_GetChanResult(ISOSDK_SDK_ADC_INSTANCE, ISOSDK_ADC_GROUP, &raw_value);

    if (raw_value > 4095U)
    {
        raw_value = 4095U;
    }

    *out_raw_value = raw_value;
    return 1U;
}

#else

// ADC가 없는 빌드에서는 지원하지 않음을 분명히 알린다.
uint8_t IsoSdk_AdcIsSupported(void)
{
    return 0U;
}

// 미지원 빌드용 stub이다.
uint8_t IsoSdk_AdcInit(IsoSdkAdcContext *context)
{
    (void)context;
    return 0U;
}

// 미지원 빌드에서는 sample 요청을 실패로 반환한다.
uint8_t IsoSdk_AdcSample(IsoSdkAdcContext *context, uint16_t *out_raw_value)
{
    (void)context;

    if (out_raw_value != NULL)
    {
        *out_raw_value = 0U;
    }

    return 0U;
}

#endif

// 참고:
// calibration과 sample은 단순한 bring-up 흐름에 맞춰져 있어서,
// 변환 시간이나 다중 채널 확장이 필요해지면 비동기 sampling 구조로 한 단계 더 나눌 여지가 있다.
