// 보드별 clock, pin, GPIO 접근을 SDK 호출로 묶어 둔 구현 파일이다.
// 상위 계층은 generated 설정 이름을 몰라도 되고,
// 이 파일만 보면 현재 보드 자원이 어디에 연결됐는지 따라갈 수 있다.
#include "isosdk_board.h"

#include <stddef.h>
#include <string.h>

#include "pins_driver.h"

#include "isosdk_board_profile.h"
#include "isosdk_sdk_bindings.h"

#ifdef ISOSDK_SDK_HAS_PWM
static ftm_state_t s_iso_sdk_pwm_state;
static uint8_t     s_iso_sdk_pwm_initialized;

// PWM duty 범위를 driver가 기대하는 0..FTM_MAX_DUTY_CYCLE로 맞춘다.
static uint16_t IsoSdk_PwmClampDuty(uint16_t duty)
{
    return (duty > FTM_MAX_DUTY_CYCLE) ? FTM_MAX_DUTY_CYCLE : duty;
}

// generated independent channel index를 실제 hw channel/polarity로 푼다.
static uint8_t IsoSdk_PwmResolveChannel(uint8_t channel_index,
                                        uint8_t *out_hw_channel,
                                        ftm_polarity_t *out_polarity)
{
    if ((out_hw_channel == NULL) ||
        (out_polarity == NULL) ||
        (channel_index >= ISOSDK_SDK_PWM_CHANNEL_COUNT))
    {
        return 0U;
    }

    *out_hw_channel = ISOSDK_SDK_PWM_INDEPENDENT_CONFIG[channel_index].hwChannelId;
    *out_polarity = ISOSDK_SDK_PWM_INDEPENDENT_CONFIG[channel_index].polarity;
    return 1U;
}

// generated polarity와 LED active level이 같은지 보고
// logical duty를 실제 FTM duty 값으로 바꾼다.
static uint16_t IsoSdk_PwmToDriverDuty(uint16_t logical_active_duty,
                                       uint8_t active_on_level,
                                       ftm_polarity_t pwm_polarity)
{
    uint8_t  led_active_low;
    uint8_t  pwm_active_low;
    uint16_t clamped_duty;

    led_active_low = (active_on_level == 0U) ? 1U : 0U;
    pwm_active_low = (pwm_polarity == FTM_POLARITY_LOW) ? 1U : 0U;
    clamped_duty = IsoSdk_PwmClampDuty(logical_active_duty);

    if (led_active_low == pwm_active_low)
    {
        return clamped_duty;
    }

    return (uint16_t)(FTM_MAX_DUTY_CYCLE - clamped_duty);
}
#endif

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
// slave 노드도 버스 응답을 해야 하므로,
// bring-up 직후 이 경로를 열어 두는 의미가 있다.
void IsoSdk_BoardEnableLinTransceiver(void)
{
#ifdef ISOSDK_SDK_HAS_LIN
    PINS_DRV_SetPinsDirection(ISOSDK_BOARD_PROFILE_LIN_XCVR_ENABLE_PORT,
                              ISOSDK_BOARD_PROFILE_LIN_XCVR_ENABLE_MASK);
    PINS_DRV_SetPins(ISOSDK_BOARD_PROFILE_LIN_XCVR_ENABLE_PORT,
                     ISOSDK_BOARD_PROFILE_LIN_XCVR_ENABLE_MASK);
#endif
}

// 현재 보드 설정에서 PWM을 쓸 수 있는지 알려준다.
uint8_t IsoSdk_PwmIsSupported(void)
{
#ifdef ISOSDK_SDK_HAS_PWM
    return 1U;
#else
    return 0U;
#endif
}

// generated flexTimer_pwm_1 설정을 한 번만 초기화한다.
uint8_t IsoSdk_PwmInit(void)
{
#ifdef ISOSDK_SDK_HAS_PWM
    status_t status;

    if (s_iso_sdk_pwm_initialized != 0U)
    {
        return 1U;
    }

    (void)memset(&s_iso_sdk_pwm_state, 0, sizeof(s_iso_sdk_pwm_state));

    status = FTM_DRV_Init(ISOSDK_SDK_PWM_INSTANCE,
                          &ISOSDK_SDK_PWM_INIT_CONFIG,
                          &s_iso_sdk_pwm_state);
    if (status != STATUS_SUCCESS)
    {
        return 0U;
    }

    status = FTM_DRV_InitPwm(ISOSDK_SDK_PWM_INSTANCE, &ISOSDK_SDK_PWM_CONFIG);
    if (status != STATUS_SUCCESS)
    {
        return 0U;
    }

    s_iso_sdk_pwm_initialized = 1U;
    return 1U;
#else
    return 0U;
#endif
}

// logical duty를 현재 LED active polarity에 맞는 PWM duty로 반영한다.
uint8_t IsoSdk_PwmWriteLogicalDuty(uint8_t channel_index,
                                   uint16_t logical_active_duty,
                                   uint8_t active_on_level)
{
#ifdef ISOSDK_SDK_HAS_PWM
    uint8_t        hw_channel;
    ftm_polarity_t pwm_polarity;
    uint16_t       driver_duty;
    status_t       status;

    if (IsoSdk_PwmResolveChannel(channel_index, &hw_channel, &pwm_polarity) == 0U)
    {
        return 0U;
    }

    if (IsoSdk_PwmInit() == 0U)
    {
        return 0U;
    }

    driver_duty = IsoSdk_PwmToDriverDuty(logical_active_duty,
                                         active_on_level,
                                         pwm_polarity);

    status = FTM_DRV_UpdatePwmChannel(ISOSDK_SDK_PWM_INSTANCE,
                                      hw_channel,
                                      FTM_PWM_UPDATE_IN_DUTY_CYCLE,
                                      driver_duty,
                                      0U,
                                      true);
    return (status == STATUS_SUCCESS) ? 1U : 0U;
#else
    (void)channel_index;
    (void)logical_active_duty;
    (void)active_on_level;
    return 0U;
#endif
}

// 상위 계층이 의미 기반 "켜짐 duty"를 만들 때 쓸 최대값이다.
uint16_t IsoSdk_PwmGetMaxDuty(void)
{
#ifdef ISOSDK_SDK_HAS_PWM
    return FTM_MAX_DUTY_CYCLE;
#else
    return 0U;
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

// 버튼 입력을 눌림 여부 형태로 정리해 읽어 온다.
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
