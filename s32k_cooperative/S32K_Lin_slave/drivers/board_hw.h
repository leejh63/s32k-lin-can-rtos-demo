#ifndef BOARD_HW_H
#define BOARD_HW_H

#include "../core/infra_types.h"
#include "led_module.h"

InfraStatus BoardHw_Init(void);
void        BoardHw_EnableLinTransceiver(void);
InfraStatus BoardHw_GetRgbLedConfig(LedConfig *out_config);
uint8_t     BoardHw_ReadSlave1ButtonPressed(void);

#endif
