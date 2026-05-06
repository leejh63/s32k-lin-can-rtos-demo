#ifndef ISOSDK_ADC_H
#define ISOSDK_ADC_H

#include <stdint.h>

typedef struct
{
    uint8_t initialized;
} IsoSdkAdcContext;

// IsoSdk_Adc 지원 여부를 확인한다.
uint8_t IsoSdk_AdcIsSupported(void);
// IsoSdk_Adc를 초기화한다.
uint8_t IsoSdk_AdcInit(IsoSdkAdcContext *context);
// IsoSdk_Adc 샘플 하나를 읽는다.
uint8_t IsoSdk_AdcSample(IsoSdkAdcContext *context, uint16_t *out_raw_value);

#endif
