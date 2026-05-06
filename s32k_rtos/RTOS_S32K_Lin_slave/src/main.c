// 역할별 프로그램의 시작점이다.
// main은 최대한 얇게 두고, 보드 초기화와
// task 등록, 역할별 동작은 runtime 계층에서 맡는다.
#include "runtime/runtime.h"

// 프로그램 전체 초기화와 FreeRTOS runtime 진입만 맡는 시작점이다.
// main에서 세부 초기화 순서를 직접 펼치지 않고 runtime으로 넘겨,
// 역할별 조립과 실패 처리 위치를 한곳으로 모은다.
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
