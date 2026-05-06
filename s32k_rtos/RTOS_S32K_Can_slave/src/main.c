// 역할별 프로그램 시작점이다.
// 실행 파일은 최대한 얇게 두고, 보드 초기화와
// task 등록, 역할별 동작은 runtime 계층에서 맡는다.
#include "runtime/runtime.h"

// 역할별 진입점이다.
// 여기서는 초기화 성공만 확인하고,
// 실제 동작은 runtime에서 native FreeRTOS task들을 띄워 맡긴다.
// 주의: 초기화 실패 시 현재는 진단 출력 없이 정지한다.
int main(void)
{
    if (Runtime_Init() != INFRA_STATUS_OK)
    {
        for (;;)
        {
        }
    }

    Runtime_Run();
    return 0;
}
