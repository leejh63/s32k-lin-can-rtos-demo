#ifndef ADC_MODULE_INTERNAL_H
#define ADC_MODULE_INTERNAL_H

#include "adc_module.h"

typedef enum
{
    ADC_SAMPLE_STATE_EMPTY = 0,
    ADC_SAMPLE_STATE_VALID,
    ADC_SAMPLE_STATE_FAULT
} AdcSampleState;

struct AdcModule
{
    uint8_t        initialized;
    AdcConfig      config;
    AdcSnapshot    snapshot;
    uint32_t       last_sample_ms;
    AdcSampleState sample_state;
};

#endif
