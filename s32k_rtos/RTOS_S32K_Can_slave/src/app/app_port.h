// app 계층이 사용하는 제품 의미 기반 보드 포트 인터페이스다.
// slave1 정책은 버튼과 상태 LED만 알면 되고,
// 실제 보드 배선과 SDK 호출은 아래 계층에 남긴다.
#ifndef APP_PORT_H
#define APP_PORT_H

#include "../core/infra_types.h"
#include "../drivers/led_module.h"

InfraStatus AppPort_GetSlave1LedConfig(LedConfig *out_config);
uint8_t     AppPort_ReadSlave1ButtonPressed(void);

#endif
