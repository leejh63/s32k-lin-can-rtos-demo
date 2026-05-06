#ifndef ISOSDK_ADC_H
#define ISOSDK_ADC_H

#include <stdint.h>

typedef struct
{
    uint8_t initialized;
} IsoSdkAdcContext;

uint8_t IsoSdk_AdcIsSupported(void);
uint8_t IsoSdk_AdcInit(IsoSdkAdcContext *context);
uint8_t IsoSdk_AdcSample(IsoSdkAdcContext *context, uint16_t *out_raw_value);

#endif
