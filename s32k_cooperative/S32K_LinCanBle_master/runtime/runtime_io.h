// master 노드가 실제로 사용하는 프로젝트 바인딩 인터페이스다.
// coordinator는 LIN master 설정과 event 연결만 필요하므로,
// 하드웨어 세부사항은 shared driver 계층 아래로 숨긴다.
#ifndef RUNTIME_IO_H
#define RUNTIME_IO_H

#include "../core/infra_types.h"
#include "../services/lin_module.h"

InfraStatus RuntimeIo_BoardInit(void);
uint8_t     RuntimeIo_GetLocalNodeId(void);
InfraStatus RuntimeIo_GetMasterLinConfig(LinConfig *out_config);
void        RuntimeIo_AttachLinModule(LinModule *module);

#endif
