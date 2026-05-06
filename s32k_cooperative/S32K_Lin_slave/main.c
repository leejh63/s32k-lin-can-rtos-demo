// 역할별 프로그램의 시작점이다.
// main은 최대한 얇게 두고, 보드 초기화와
// task 등록, 역할별 동작은 runtime 계층에서 맡는다.
#include "runtime/runtime.h"

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
