// 최상위 runtime 인터페이스다.
// 애플리케이션은 이 함수들만 호출하면 시스템을 초기화하고,
// 무한 cooperative scheduler로 진입할 수 있다.
#ifndef RUNTIME_H
#define RUNTIME_H

#include "../core/infra_types.h"

InfraStatus Runtime_Init(void);
void        Runtime_Run(void);

#endif
