// 로컬 보드 PWM 설정에 맞춘 LED 패턴 실행 구현 파일이다.
// 의미 기반 패턴을 PWM duty write로 바꾸고,
// 주기 LED task 동안 blink 시퀀스를 진행시킨다.
#include "led_module.h"

#include <stddef.h>
#include <string.h>

#include "../platform/s32k_sdk/isosdk_board.h"

// LED 한 채널 duty를 현재 config가 허용하는 최대 duty 안으로 맞춘다.
static uint16_t LedModule_ClampDuty(const LedModule *module, uint16_t duty)
{
    if (module == NULL)
    {
        return 0U;
    }

    return (duty > module->config.on_duty) ? module->config.on_duty : duty;
}

// LED 한 채널을 현재 PWM/LED polarity 조합에 맞는 duty로 바로 출력한다.
// 상위 계층은 logical duty만 전달하고,
// 실제 active-low 보정은 PWM wrapper에서 처리하도록 한다.
static void LedModule_WriteChannelDuty(const LedModule *module, uint8_t channel_index, uint16_t duty)
{
    if (module == NULL)
    {
        return;
    }

    (void)IsoSdk_PwmWriteLogicalDuty(channel_index,
                                     LedModule_ClampDuty(module, duty),
                                     module->config.active_on_level);
}

// LED 한 채널을 현재 PWM/LED polarity 조합에 맞는 duty로 바로 출력한다.
// 상위 계층은 on/off 의미만 전달하고,
// 실제 logical duty와 active-low 보정은 PWM wrapper에서 처리하도록 한다.
static void LedModule_WriteChannel(const LedModule *module, uint8_t channel_index, uint8_t on)
{
    LedModule_WriteChannelDuty(module,
                               channel_index,
                               (on != 0U) ? module->config.on_duty : 0U);
}

// 빨강과 초록 두 출력을 한 번에 반영한다.
// pattern 해석과 실제 channel update를 분리해 두면,
// 상위 상태 전이의 가독성이 좋아진다.
static void LedModule_ApplyOutputs(const LedModule *module, uint8_t red_on, uint8_t green_on)
{
    if (module == NULL)
    {
        return;
    }

    LedModule_WriteChannel(module, module->config.red_channel_index, red_on);
    LedModule_WriteChannel(module, module->config.green_channel_index, green_on);
}

// 빨강과 초록 duty를 한 번에 반영한다.
// 연속 색 전환은 이 helper를 통해 PWM mix로 표현한다.
static void LedModule_ApplyDutyOutputs(const LedModule *module, uint16_t red_duty, uint16_t green_duty)
{
    if (module == NULL)
    {
        return;
    }

    LedModule_WriteChannelDuty(module, module->config.red_channel_index, red_duty);
    LedModule_WriteChannelDuty(module, module->config.green_channel_index, green_duty);
}

// 의미 기반 LED 패턴을 현재 시점의 실제 출력 조합으로 바꾼다.
// blink 계열은 phase만 보고 on/off를 정하고,
// solid 계열은 항상 같은 상태를 유지한다.
static void LedModule_ApplyPattern(const LedModule *module, LedPattern pattern, uint8_t phase_on)
{
    switch (pattern)
    {
        case LED_PATTERN_GREEN_SOLID:
            LedModule_ApplyOutputs(module, 0U, 1U);
            break;

        case LED_PATTERN_RED_SOLID:
            LedModule_ApplyOutputs(module, 1U, 0U);
            break;

        case LED_PATTERN_YELLOW_SOLID:
            LedModule_ApplyOutputs(module, 1U, 1U);
            break;

        case LED_PATTERN_CUSTOM_SOLID:
            LedModule_ApplyDutyOutputs(module,
                                       module->solid_red_duty,
                                       module->solid_green_duty);
            break;

        case LED_PATTERN_RED_BLINK:
            LedModule_ApplyOutputs(module, phase_on, 0U);
            break;

        case LED_PATTERN_GREEN_BLINK:
            LedModule_ApplyOutputs(module, 0U, phase_on);
            break;

        case LED_PATTERN_OFF:
        default:
            LedModule_ApplyOutputs(module, 0U, 0U);
            break;
    }
}

// 로컬 LED 제어기를 초기 상태로 만든다.
// PWM channel 정보와 active level을 받아 두고,
// 시작 시점 출력은 모두 꺼진 상태로 맞춘다.
InfraStatus LedModule_Init(LedModule *module, const LedConfig *config)
{
    if ((module == NULL) || (config == NULL) || (config->on_duty == 0U))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    if (IsoSdk_PwmInit() == 0U)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    (void)memset(module, 0, sizeof(*module));
    module->config = *config;
    module->pattern = LED_PATTERN_OFF;
    module->output_phase_on = 0U;
    module->solid_red_duty = 0U;
    module->solid_green_duty = 0U;

    LedModule_ApplyPattern(module, module->pattern, 0U);
    module->initialized = 1U;

    return INFRA_STATUS_OK;
}

// 현재 표시할 패턴을 바로 바꾼다.
// 새 패턴이 들어오면 이전 blink 진행 상태는 정리하고,
// 지금 보여야 할 첫 출력도 즉시 반영한다.
void LedModule_SetPattern(LedModule *module, LedPattern pattern)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    module->pattern = pattern;
    module->finite_blink_enabled = 0U;
    module->blink_toggles_remaining = 0U;
    module->solid_red_duty = 0U;
    module->solid_green_duty = 0U;
    module->output_phase_on = (pattern == LED_PATTERN_RED_BLINK ||
                               pattern == LED_PATTERN_GREEN_BLINK) ? 1U : 0U;

    LedModule_ApplyPattern(module, module->pattern, module->output_phase_on);
}

// 빨강/초록 duty를 직접 지정해 연속적인 고정 색을 반영한다.
// gradient LED 같은 출력은 blink pattern 대신 이 API를 사용한다.
void LedModule_SetCustomSolid(LedModule *module, uint16_t red_duty, uint16_t green_duty)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    module->pattern = LED_PATTERN_CUSTOM_SOLID;
    module->finite_blink_enabled = 0U;
    module->blink_toggles_remaining = 0U;
    module->output_phase_on = 0U;
    module->solid_red_duty = LedModule_ClampDuty(module, red_duty);
    module->solid_green_duty = LedModule_ClampDuty(module, green_duty);

    LedModule_ApplyPattern(module, module->pattern, 0U);
}

// 승인 완료를 짧게 보여 주는 초록 blink 시퀀스를 시작한다.
// 무한 blink와 달리 정해 둔 횟수만 토글한 뒤,
// 자동으로 꺼지는 흐름을 위해 따로 분리한 함수다.
void LedModule_StartGreenAckBlink(LedModule *module, uint8_t toggle_count)
{
    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    module->pattern = LED_PATTERN_GREEN_BLINK;
    module->output_phase_on = 1U;
    module->finite_blink_enabled = 1U;
    module->blink_toggles_remaining = toggle_count;

    LedModule_ApplyPattern(module, module->pattern, module->output_phase_on);
}

// blink 패턴의 다음 출력 상태를 한 단계 진행시킨다.
// 유한 blink가 끝나면 off로 정리해서,
// 상위 상태기계가 별도 정지 명령 없이도 다음 단계로 넘어가게 한다.
// 주의: 지금 점멸 속도는 now_ms가 아니라 task 호출 주기에 기대고 있다.
// 현재 규모에서는 충분하지만, 더 일정한 점멸이 필요하면 시간 간격 기반으로 바꾸는 편이 낫다.
void LedModule_Task(LedModule *module, uint32_t now_ms)
{
    (void)now_ms;

    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    // blink 패턴이 아닐 때는 현재 출력 상태를 그대로 유지한다.
    if ((module->pattern != LED_PATTERN_RED_BLINK) &&
        (module->pattern != LED_PATTERN_GREEN_BLINK))
    {
        return;
    }

    // 한 번 호출될 때마다 on/off 위상을 뒤집는다.
    module->output_phase_on = (module->output_phase_on == 0U) ? 1U : 0U;

    if (module->finite_blink_enabled != 0U)
    {
        if (module->blink_toggles_remaining > 0U)
        {
            module->blink_toggles_remaining--;
        }

        if (module->blink_toggles_remaining == 0U)
        {
            module->finite_blink_enabled = 0U;
            module->pattern = LED_PATTERN_OFF;
            module->output_phase_on = 0U;
            LedModule_ApplyPattern(module, module->pattern, module->output_phase_on);
            return;
        }
    }

    LedModule_ApplyPattern(module, module->pattern, module->output_phase_on);
}

// 외부에서 현재 패턴 상태를 조회한다.
// acknowledgement blink가 끝났는지처럼,
// 상위 상태기계가 후속 동작을 결정할 때 사용한다.
LedPattern LedModule_GetPattern(const LedModule *module)
{
    if (module == NULL)
    {
        return LED_PATTERN_OFF;
    }

    return module->pattern;
}

// 참고:
// blink 진행은 task 호출 주기에 기대고 있어서,
// 실제 점멸 속도를 더 일정하게 맞추려면 now_ms를 활용한 간격 제어를 추후 덧붙일 수 있다.
