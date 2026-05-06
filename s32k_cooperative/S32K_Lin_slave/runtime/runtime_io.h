// LIN sensor slave가 실제로 사용하는 프로젝트 바인딩 인터페이스다.
// slave2는 ADC, LED, LIN slave 설정만 필요하므로,
// console, CAN, 버튼 관련 API는 제거해 읽기 쉽게 만든다.
#ifndef RUNTIME_IO_H
#define RUNTIME_IO_H

#include "../services/adc_module.h"
#include "../core/infra_types.h"
#include "../drivers/led_module.h"
#include "../services/lin_module.h"

InfraStatus RuntimeIo_BoardInit(void);
uint8_t     RuntimeIo_GetLocalNodeId(void);
InfraStatus RuntimeIo_GetSlave2LedConfig(LedConfig *out_config);
InfraStatus RuntimeIo_GetSlave2AdcConfig(AdcConfig *out_config);
InfraStatus RuntimeIo_GetSlaveLinConfig(LinConfig *out_config);
void        RuntimeIo_AttachLinModule(LinModule *module);

#endif
