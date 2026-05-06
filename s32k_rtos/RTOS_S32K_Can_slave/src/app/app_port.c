// app 계층에서 쓰는 제품 포트의 구체 구현 파일이다.
// app은 이 포트만 보고,
// board 의미와 SDK 적응 계층은 drivers 아래에 숨긴다.
#include "app_port.h"

#include "../drivers/board_hw.h"

// slave1이 쓸 LED 배선 정보를 app 친화적인 형태로 넘긴다.
// app 쪽에서는 보드 핀 배치나 active level을 따로 알 필요 없이,
// 이 포트에서 받은 설정만 바로 LED 모듈로 넘기면 된다.
InfraStatus AppPort_GetSlave1LedConfig(LedConfig *out_config)
{
    return BoardHw_GetSlave1LedConfig(out_config);
}

// slave1 승인 버튼의 현재 눌림 상태를 읽어 온다.
// 버튼 판독의 세부 방식은 하위 보드 계층에 맡기고,
// app 쪽에는 눌림 여부만 짧게 전달한다.
uint8_t AppPort_ReadSlave1ButtonPressed(void)
{
    return BoardHw_ReadSlave1ButtonPressed();
}
