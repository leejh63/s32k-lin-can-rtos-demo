// LPTMR 기반 system tick 초기화를 감싼 구현 파일이다.
// runtime 계층은 어떤 timer driver를 쓰는지 모르고,
// handler 등록과 compare flag clear만 사용하면 되게 한다.
#include "isosdk_tick.h"

#include <stddef.h>

#include "interrupt_manager.h"

#include "isosdk_sdk_bindings.h"

// tick timer를 시작하고 IRQ handler를 연결한다.
uint8_t IsoSdk_TickInit(IsoSdkTickHandler handler)
{
    if (handler == NULL)
    {
        return 0U;
    }

    LPTMR_DRV_Init(ISOSDK_SDK_LPTMR_INSTANCE, &ISOSDK_SDK_LPTMR_CONFIG, false);
    INT_SYS_InstallHandler(ISOSDK_SDK_LPTMR_IRQ, (isr_t)handler, (isr_t *)NULL);
    INT_SYS_EnableIRQ(ISOSDK_SDK_LPTMR_IRQ);
    LPTMR_DRV_StartCounter(ISOSDK_SDK_LPTMR_INSTANCE);
    return 1U;
}

// timer compare flag를 정리해 다음 tick을 받을 수 있게 한다.
void IsoSdk_TickClearCompareFlag(void)
{
    LPTMR_DRV_ClearCompareFlag(ISOSDK_SDK_LPTMR_INSTANCE);
}

// 참고:
// tick 주기와 실제 task period 해상도는 generated timer 설정에 그대로 묶여 있으니,
// scheduler 주기를 조정할 때는 runtime 쪽 숫자만이 아니라 timer 설정도 같이 봐야 한다.
