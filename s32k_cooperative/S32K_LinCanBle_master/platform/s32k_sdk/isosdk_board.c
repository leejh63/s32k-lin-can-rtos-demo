// 보드별 clock, pin, GPIO 접근을 SDK 호출로 묶어 둔 구현 파일이다.
// 상위 계층은 generated 설정 이름을 몰라도 되고,
// 이 파일만 보면 현재 보드 자원이 어디에 연결됐는지 따라갈 수 있다.
#include "isosdk_board.h"

#include <stddef.h>

#include "pins_driver.h"

#include "isosdk_board_profile.h"
#include "isosdk_sdk_bindings.h"

// 보드 공통 초기화를 한 번에 수행한다.
// clock 설정을 적용하고 pin mux를 올려,
// 이후 GPIO와 주변장치가 기대한 배선으로 동작할 준비를 마친다.
uint8_t IsoSdk_BoardInit(void)
{
    status_t status;

    status = CLOCK_SYS_Init(ISOSDK_SDK_CLOCK_CONFIGS,
                            ISOSDK_SDK_CLOCK_CONFIG_COUNT,
                            ISOSDK_SDK_CLOCK_CALLBACKS,
                            ISOSDK_SDK_CLOCK_CALLBACK_COUNT);
    if (status != STATUS_SUCCESS)
    {
        return 0U;
    }

    status = CLOCK_SYS_UpdateConfiguration(0U, CLOCK_MANAGER_POLICY_AGREEMENT);
    if (status != STATUS_SUCCESS)
    {
        return 0U;
    }

    PINS_DRV_Init(ISOSDK_SDK_PIN_CONFIG_COUNT, ISOSDK_SDK_PIN_CONFIGS);
    return 1U;
}

// LIN 트랜시버 enable 핀을 켠다.
// master 노드는 LIN 버스를 직접 다루므로,
// 보드 bring-up 직후 이 경로를 열어 두는 의미가 있다.
void IsoSdk_BoardEnableLinTransceiver(void)
{
#ifdef ISOSDK_SDK_HAS_LIN
    PINS_DRV_SetPinsDirection(ISOSDK_BOARD_PROFILE_LIN_XCVR_ENABLE_PORT,
                              ISOSDK_BOARD_PROFILE_LIN_XCVR_ENABLE_MASK);
    PINS_DRV_SetPins(ISOSDK_BOARD_PROFILE_LIN_XCVR_ENABLE_PORT,
                     ISOSDK_BOARD_PROFILE_LIN_XCVR_ENABLE_MASK);
#endif
}

// RGB LED가 연결된 GPIO port를 반환한다.
void *IsoSdk_BoardGetRgbLedPort(void)
{
    return ISOSDK_BOARD_PROFILE_RGB_LED_PORT;
}

// RGB LED의 빨강 채널 pin 번호를 알려준다.
uint32_t IsoSdk_BoardGetRgbLedRedPin(void)
{
    return ISOSDK_BOARD_PROFILE_RGB_LED_RED_PIN;
}

// RGB LED의 초록 채널 pin 번호를 알려준다.
uint32_t IsoSdk_BoardGetRgbLedGreenPin(void)
{
    return ISOSDK_BOARD_PROFILE_RGB_LED_GREEN_PIN;
}

// LED가 켜지는 논리 레벨을 보드 설정에서 가져온다.
uint8_t IsoSdk_BoardGetRgbLedActiveOnLevel(void)
{
    return ISOSDK_BOARD_PROFILE_RGB_LED_ACTIVE_ON_LEVEL;
}

// slave1 버튼 입력을 눌림 여부 형태로 정리해 읽어 온다.
// 하드웨어는 active-low 배선이더라도,
// 상위 계층은 값이 1이면 pressed라는 의미만 해석하면 되도록 맞춘다.
uint8_t IsoSdk_BoardReadSlave1ButtonPressed(void)
{
    GPIO_Type *gpio_port;

    gpio_port = (GPIO_Type *)ISOSDK_BOARD_PROFILE_SLAVE1_BUTTON_PORT;
    return ((PINS_DRV_ReadPins(gpio_port) & ISOSDK_BOARD_PROFILE_SLAVE1_BUTTON_MASK) == 0U) ? 1U : 0U;
}

// 상위 계층이 직접 SDK GPIO 타입을 몰라도 pin 하나를 쓸 수 있게 한다.
void IsoSdk_GpioWritePin(void *gpio_port, uint32_t pin, uint8_t level)
{
    if (gpio_port == NULL)
    {
        return;
    }

    PINS_DRV_WritePin((GPIO_Type *)gpio_port, pin, level);
}

// 여러 pin의 direction을 한 번에 output으로 맞춘다.
void IsoSdk_GpioSetPinsDirectionMask(void *gpio_port, uint32_t pin_mask)
{
    if (gpio_port == NULL)
    {
        return;
    }

    PINS_DRV_SetPinsDirection((GPIO_Type *)gpio_port,
                              (pins_channel_type_t)pin_mask);
}

// 참고:
// 보드 프로필 값이 바뀌면 이 파일은 그대로여도 동작이 달라지므로,
// 실제 배선 변경이 있을 때는 profile과 여기서 읽는 의미가 계속 맞는지 함께 확인해 두는 것이 적절하다.
