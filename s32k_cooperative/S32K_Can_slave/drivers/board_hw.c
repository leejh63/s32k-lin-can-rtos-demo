// 보드에 묶인 최소 하드웨어 정보를 모아 주는 구현 파일이다.
// 상위 계층은 역할 이름으로 LED와 버튼을 요청하고,
// 실제 핀 배치나 초기화 순서는 이 파일 안에만 유지한다.
#include "board_hw.h"

#include <stddef.h>
#include <string.h>

#include "../platform/s32k_sdk/isosdk_board.h"

// 프로젝트가 기대하는 기본 보드 준비를 한 번에 시작한다.
// 클럭과 핀 초기화 같은 시작 직후 작업은 IsoSdk 쪽에서 맡고,
// 여기서는 성공 여부를 공통 상태 코드로 변환한다.
InfraStatus BoardHw_Init(void)
{
    return (IsoSdk_BoardInit() != 0U) ? INFRA_STATUS_OK : INFRA_STATUS_IO_ERROR;
}

// slave1이 쓰는 RGB LED 정보를 설정 구조체에 채운다.
// 의미 이름으로 묶어 두면,
// 추후 보드 핀이 바뀌어도 app이나 LED 모듈까지 수정이 퍼지지 않는다.
InfraStatus BoardHw_GetSlave1LedConfig(LedConfig *out_config)
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

// slave1 전용 버튼 입력을 읽어 눌림 여부만 반환한다.
// 디바운스나 상태 전이는 여기서 하지 않고,
// 상위 app task가 같은 규칙으로 처리하도록 한다.
uint8_t BoardHw_ReadSlave1ButtonPressed(void)
{
    return IsoSdk_BoardReadSlave1ButtonPressed();
}
