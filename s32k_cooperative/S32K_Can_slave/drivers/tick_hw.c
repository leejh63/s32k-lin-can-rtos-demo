// 시스템 tick용 하드웨어 어댑터 구현 파일이다.
// runtime 쪽에서는 비교적 단순한 시작 함수만 호출하고,
// 구체적인 타이머 driver 연결은 IsoSdk 계층에 위임한다.
#include "tick_hw.h"

#include <stddef.h>

#include "../platform/s32k_sdk/isosdk_tick.h"

// 공통 tick ISR handler를 하드웨어 타이머에 연결한다.
// 호출자는 handler만 넘기면 되고,
// 어떤 타이머 인스턴스를 쓰는지는 하위 binding이 정한다.
InfraStatus TickHw_Init(TickHwHandler handler)
{
    if (handler == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    return (IsoSdk_TickInit(handler) != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// 다음 tick interrupt를 위해 비교 플래그를 정리한다.
// ISR 본문에서는 시간을 올리고 hook를 호출한 뒤,
// 마지막에 이 함수를 통해 하드웨어 쪽 마무리를 한다.
void TickHw_ClearCompareFlag(void)
{
    IsoSdk_TickClearCompareFlag();
}
