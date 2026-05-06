// master 노드가 실제로 사용하는 프로젝트 바인딩 인터페이스다.
// coordinator는 LIN master 설정과 event 연결만 필요하므로,
// 하드웨어 세부사항은 shared driver 계층 아래로 숨긴다.
#ifndef RUNTIME_IO_H
#define RUNTIME_IO_H

#include "../core/infra_types.h"
#include "../services/lin_module.h"

// runtime에서 사용하는 보드 자원을 초기화한다.
InfraStatus RuntimeIo_BoardInit(void);
// 현재 빌드가 사용하는 로컬 node id를 조회한다.
uint8_t     RuntimeIo_GetLocalNodeId(void);
// master용 LIN 설정을 채워 준다.
InfraStatus RuntimeIo_GetMasterLinConfig(LinConfig *out_config);
// LIN 모듈과 runtime 하드웨어 bridge를 연결한다.
void        RuntimeIo_AttachLinModule(LinModule *module);

#endif
