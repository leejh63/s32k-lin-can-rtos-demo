// 의미 기반 LED 제어 인터페이스다.
// 호출자는 raw GPIO polarity나 pin 순서를 몰라도,
// 빨강 고정이나 초록 점멸 같은 패턴만 요청하면 된다.
#ifndef LED_MODULE_H
#define LED_MODULE_H

#include "../core/infra_types.h"

typedef enum
{
    LED_PATTERN_OFF = 0,
    LED_PATTERN_GREEN_SOLID,
    LED_PATTERN_RED_SOLID,
    LED_PATTERN_YELLOW_SOLID,
    LED_PATTERN_RED_BLINK,
    LED_PATTERN_GREEN_BLINK
} LedPattern;

// LED 모듈에 필요한 보드 배선 정보 구조체다.
// Runtime IO가 이 값을 채워 넣어,
// LED 로직이 concrete GPIO 정의와 분리된 의미 기반 코드로 남게 한다.
typedef struct
{
    void    *gpio_port;
    uint32_t red_pin;
    uint32_t green_pin;
    uint8_t  active_on_level;
} LedConfig;

// 하나의 로컬 출력 장치에 대한 전체 LED 모듈 상태다.
// 현재 pattern과 blink phase,
// finite acknowledgement 시퀀스 진행 여부를 함께 기억한다.
typedef struct
{
    uint8_t    initialized;
    LedConfig  config;
    LedPattern pattern;
    uint8_t    output_phase_on;
    uint8_t    finite_blink_enabled;
    uint8_t    blink_toggles_remaining;
} LedModule;

InfraStatus LedModule_Init(LedModule *module, const LedConfig *config);
void        LedModule_SetPattern(LedModule *module, LedPattern pattern);
void        LedModule_StartGreenAckBlink(LedModule *module, uint8_t toggle_count);
void        LedModule_Task(LedModule *module, uint32_t now_ms);
LedPattern  LedModule_GetPattern(const LedModule *module);

#endif
