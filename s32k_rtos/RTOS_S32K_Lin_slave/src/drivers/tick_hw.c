// system tick 하드웨어 초기화를 감싼 아주 얇은 바인딩 구현 파일이다.
// runtime은 어떤 timer driver를 쓰는지 몰라도 되고,
// 이 계층이 handler 등록과 flag clear만 대신 맡는다.
#include "tick_hw.h"

#include <stddef.h>

#include "../platform/s32k_sdk/isosdk_tick.h"

// tick ISR handler를 실제 하드웨어 timer에 연결한다.
InfraStatus TickHw_Init(TickHwHandler handler)
{
    if (handler == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    return (IsoSdk_TickInit(handler) != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// 다음 tick을 받을 수 있게 compare flag를 지운다.
void TickHw_ClearCompareFlag(void)
{
    IsoSdk_TickClearCompareFlag();
}
