// 로컬 보드 배선에 맞춘 LED 패턴 실행 구현 파일이다.
// 의미 기반 패턴을 GPIO write로 바꾸고,
// 주기 LED task 동안 blink 시퀀스를 진행시킨다.
#include "led_module.h"

#include <stddef.h>
#include <string.h>

#include "../platform/s32k_sdk/isosdk_board.h"

// LED 한 채널을 현재 보드 극성에 맞춰 바로 출력한다.
// 상위 계층은 on/off 의미만 전달하고,
// 실제 high/low 변환은 이 계층에서만 처리하도록 한다.
static void LedModule_WritePin(const LedModule *module, uint32_t pin, uint8_t on)
{
    uint8_t level;

    if ((module == NULL) || (module->config.gpio_port == NULL))
    {
        return;
    }

    level = (on != 0U) ? module->config.active_on_level :
            (uint8_t)((module->config.active_on_level == 0U) ? 1U : 0U);

    IsoSdk_GpioWritePin(module->config.gpio_port, pin, level);
}

// 빨강과 초록 두 출력을 한 번에 반영한다.
// pattern 해석과 실제 pin write를 분리해 두면,
// 상위 상태 전이의 가독성이 좋아진다.
static void LedModule_ApplyOutputs(const LedModule *module, uint8_t red_on, uint8_t green_on)
{
    if (module == NULL)
    {
        return;
    }

    LedModule_WritePin(module, module->config.red_pin, red_on);
    LedModule_WritePin(module, module->config.green_pin, green_on);
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
// 배선 정보와 active level을 받아 두고,
// 시작 시점 출력은 모두 꺼진 상태로 맞춘다.
InfraStatus LedModule_Init(LedModule *module, const LedConfig *config)
{
    if ((module == NULL) || (config == NULL) || (config->gpio_port == NULL))
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(module, 0, sizeof(*module));
    module->config = *config;
    module->pattern = LED_PATTERN_OFF;
    module->output_phase_on = 0U;

    IsoSdk_GpioSetPinsDirectionMask(module->config.gpio_port,
                                    (1UL << module->config.red_pin) |
                                    (1UL << module->config.green_pin));

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
    module->output_phase_on = (pattern == LED_PATTERN_RED_BLINK ||
                               pattern == LED_PATTERN_GREEN_BLINK) ? 1U : 0U;

    LedModule_ApplyPattern(module, module->pattern, module->output_phase_on);
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
void LedModule_Task(LedModule *module, uint32_t now_ms)
{
    (void)now_ms;

    if ((module == NULL) || (module->initialized == 0U))
    {
        return;
    }

    if ((module->pattern != LED_PATTERN_RED_BLINK) &&
        (module->pattern != LED_PATTERN_GREEN_BLINK))
    {
        return;
    }

    module->output_phase_on = (module->output_phase_on == 0U) ? 1U : 0U;

    if (module->finite_blink_enabled != 0U)
    {
        // 이 값은 blink 횟수보다 토글 횟수에 가깝다.
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
