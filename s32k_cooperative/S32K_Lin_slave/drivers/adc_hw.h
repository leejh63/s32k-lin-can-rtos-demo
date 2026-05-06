#ifndef ADC_HW_H
#define ADC_HW_H

#include <stdint.h>

#include "../core/infra_types.h"

uint8_t     AdcHw_IsSupported(void);
InfraStatus AdcHw_Init(void *context);
InfraStatus AdcHw_Sample(void *context, uint16_t *out_raw_value);

#endif
