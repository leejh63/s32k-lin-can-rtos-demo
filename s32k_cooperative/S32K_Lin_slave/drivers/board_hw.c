// 보드 전용 리소스를 공용 설정 구조로 바꾸는 작은 어댑터 구현 파일이다.
// app이나 service 계층은 generated 보드 이름 대신,
// 여기서 정리된 공용 함수만 통해 자원을 가져간다.
#include "board_hw.h"

#include <stddef.h>
#include <string.h>

#include "../platform/s32k_sdk/isosdk_board.h"

// 보드 공통 초기화를 시작한다.
InfraStatus BoardHw_Init(void)
{
    return (IsoSdk_BoardInit() != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// LIN 트랜시버 enable 핀을 켠다.
// slave라도 실제 버스 응답을 해야 하므로,
// bring-up 단계에서 이 경로를 먼저 열어 둔다.
void BoardHw_EnableLinTransceiver(void)
{
    IsoSdk_BoardEnableLinTransceiver();
}

// RGB LED 배선 정보를 공용 LedConfig 형태로 묶어 반환한다.
InfraStatus BoardHw_GetRgbLedConfig(LedConfig *out_config)
{
    if (out_config == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(out_config, 0, sizeof(*out_config));
    out_config->gpio_port = IsoSdk_BoardGetRgbLedPort();
    out_config->red_pin = IsoSdk_BoardGetRgbLedRedPin();
    out_config->green_pin = IsoSdk_BoardGetRgbLedGreenPin();
    out_config->active_on_level = IsoSdk_BoardGetRgbLedActiveOnLevel();
    return INFRA_STATUS_OK;
}

// 로컬 버튼 입력을 눌림 여부 형태로 읽어 온다.
uint8_t BoardHw_ReadSlave1ButtonPressed(void)
{
    return IsoSdk_BoardReadSlave1ButtonPressed();
}

// 참고:
// 이 계층은 얇아서 읽기 쉽지만, 보드 자원이 늘어나면 함수 수가 빠르게 많아질 수 있으니
// 확장 시에는 입출력 성격별로 파일을 더 나누는 것도 적절하다.
