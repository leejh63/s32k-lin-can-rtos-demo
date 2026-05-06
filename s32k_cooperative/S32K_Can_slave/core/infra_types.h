// 공통 인프라 타입과 시간 계산 도우미를 모아둔 헤더다.
// 대부분의 모듈이 같은 상태 코드 체계와
// wrap-safe 시간 비교 함수를 재사용하기 위해 포함한다.
#ifndef INFRA_TYPES_H
#define INFRA_TYPES_H

#include <stddef.h>
#include <stdint.h>

#define INFRA_ARRAY_COUNT(array_) \
    ((uint32_t)(sizeof(array_) / sizeof((array_)[0])))

// 모든 모듈이 공유하는 공통 상태 코드다.
// 하나의 enum 체계를 쓰면 초기화, queue,
// 통신 오류를 계층 전체에서 같은 방식으로 비교할 수 있다.
typedef enum
{
    INFRA_STATUS_OK = 0,
    INFRA_STATUS_BUSY,
    INFRA_STATUS_EMPTY,
    INFRA_STATUS_FULL,
    INFRA_STATUS_TIMEOUT,
    INFRA_STATUS_INVALID_ARG,
    INFRA_STATUS_NOT_READY,
    INFRA_STATUS_IO_ERROR,
    INFRA_STATUS_UNSUPPORTED
} InfraStatus;

// 주기 task와 timeout에서 쓰는 wrap-safe 시간 도우미다.
// runtime, UART, CAN, LIN, ADC 전반에서
// 경과 시간 비교를 읽기 쉽고 일관되게 유지한다.
static inline uint32_t Infra_TimeElapsedMs(uint32_t now_ms, uint32_t start_ms)
{
    return (uint32_t)(now_ms - start_ms);
}

static inline uint8_t Infra_TimeIsExpired(uint32_t now_ms,
                                          uint32_t start_ms,
                                          uint32_t timeout_ms)
{
    return (Infra_TimeElapsedMs(now_ms, start_ms) >= timeout_ms) ? 1U : 0U;
}

static inline uint8_t Infra_TimeIsDue(uint32_t now_ms,
                                      uint32_t last_run_ms,
                                      uint32_t period_ms)
{
    if (period_ms == 0U)
    {
        return 0U;
    }

    return (Infra_TimeElapsedMs(now_ms, last_run_ms) >= period_ms) ? 1U : 0U;
}

#endif
