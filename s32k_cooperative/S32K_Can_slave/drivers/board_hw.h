// 제품 의미 이름으로 감싼 보드 HAL adapter 인터페이스다.
// 상위 계층은 RGB 핀 번호 대신,
// slave1 버튼과 상태 LED 같은 제품 개념만 사용한다.
#ifndef BOARD_HW_H
#define BOARD_HW_H

#include "../core/infra_types.h"
#include "led_module.h"

InfraStatus BoardHw_Init(void);
InfraStatus BoardHw_GetSlave1LedConfig(LedConfig *out_config);
uint8_t     BoardHw_ReadSlave1ButtonPressed(void);

#endif
