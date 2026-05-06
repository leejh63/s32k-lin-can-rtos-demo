#ifndef BOARD_HW_H
#define BOARD_HW_H

#include "../core/infra_types.h"
#include "led_module.h"

// BoardHw를 초기화한다.
InfraStatus BoardHw_Init(void);
// BoardHw LIN transceiver를 활성화한다.
void        BoardHw_EnableLinTransceiver(void);
InfraStatus BoardHw_GetRgbLedConfig(LedConfig *out_config);
// BoardHw slave1 버튼 입력을 읽는다.
uint8_t     BoardHw_ReadSlave1ButtonPressed(void);

#endif
