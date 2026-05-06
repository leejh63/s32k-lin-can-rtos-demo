// 공용 LIN 프로토콜 상태기계 인터페이스다.
// master와 slave 역할이 같은 API를 재사용하고,
// runtime_io가 하위 계층에서 하드웨어 전용 callback 바인딩을 제공한다.
#ifndef LIN_MODULE_H
#define LIN_MODULE_H

#include "../core/infra_types.h"

typedef enum
{
    LIN_ROLE_MASTER = 1,
    LIN_ROLE_SLAVE = 2
} LinRole;

typedef enum
{
    LIN_EVENT_NONE = 0,
    LIN_EVENT_PID_OK,
    LIN_EVENT_RX_DONE,
    LIN_EVENT_TX_DONE,
    LIN_EVENT_ERROR
} LinEventId;

typedef enum
{
    LIN_ZONE_SAFE = 0,
    LIN_ZONE_WARNING,
    LIN_ZONE_DANGER,
    LIN_ZONE_EMERGENCY
} LinZone;

typedef struct LinModule LinModule;

typedef InfraStatus (*LinHwInitFn)(void *context);
typedef InfraStatus (*LinHwMasterSendHeaderFn)(void *context, uint8_t pid);
typedef InfraStatus (*LinHwStartReceiveFn)(void *context, uint8_t *buffer, uint8_t length);
typedef InfraStatus (*LinHwStartSendFn)(void *context, const uint8_t *buffer, uint8_t length);
typedef void (*LinHwGotoIdleFn)(void *context);
typedef void (*LinHwSetTimeoutFn)(void *context, uint16_t timeout_ticks);
typedef void (*LinHwServiceTickFn)(void *context);

// 공용 LIN 모듈이 사용하는 하드웨어 바인딩 callback 표다.
// Runtime IO가 이 테이블을 채워 넣어,
// 상태기계가 SDK에 직접 연결되지 않고도 실제 버스 동작을 요청하게 한다.
typedef struct
{
    LinHwInitFn             init_fn;
    LinHwMasterSendHeaderFn master_send_header_fn;
    LinHwStartReceiveFn     start_receive_fn;
    LinHwStartSendFn        start_send_fn;
    LinHwGotoIdleFn         goto_idle_fn;
    LinHwSetTimeoutFn       set_timeout_fn;
    LinHwServiceTickFn      service_tick_fn;
    void                   *context;
} LinBinding;

// LIN으로 교환되는 해석된 센서 상태 구조체다.
// 이 프레임은 ADC 값과 ADC 상태 구간,
// 그리고 latch/valid/fresh/fault 상태를 slave2와 master policy 사이에 전달한다.
typedef struct
{
    uint16_t adc_value;
    uint8_t  zone;
    uint8_t  emergency_latched;
    uint8_t  valid;
    uint8_t  fresh;
    uint8_t  fault;
} LinStatusFrame;

typedef struct
{
    uint8_t    role;
    uint8_t    pid_status;
    uint8_t    pid_ok;
    uint8_t    ok_token;
    uint8_t    status_frame_size;
    uint8_t    ok_frame_size;
    uint16_t   timeout_ticks;
    uint32_t   poll_period_ms;
    LinBinding binding;
} LinConfig;

// LinModule를 초기화한다.
InfraStatus LinModule_Init(LinModule *module, const LinConfig *config);
// LinModule에 base tick 이벤트를 전달한다.
void        LinModule_OnBaseTick(LinModule *module);
// LinModule에 하드웨어 event를 전달한다.
void        LinModule_OnEvent(LinModule *module, LinEventId event_id, uint8_t current_pid);
// 기록된 LIN event를 fast task 문맥에서 처리한다.
void        LinModule_TaskFast(LinModule *module, uint32_t now_ms);
// master용 LIN poll 슬롯을 주기적으로 시작한다.
void        LinModule_TaskPoll(LinModule *module, uint32_t now_ms);
// 다음 poll 슬롯에 보낼 OK token 요청을 적재한다.
InfraStatus LinModule_RequestOk(LinModule *module);
InfraStatus LinModule_GetLatestStatusIfFresh(const LinModule *module,
                                             uint32_t now_ms,
                                             uint32_t max_age_ms,
                                             LinStatusFrame *out_status);
// 새로 들어온 LIN status를 한 번 소비한다.
uint8_t     LinModule_ConsumeFreshStatus(LinModule *module, LinStatusFrame *out_status);
// LinModule slave status 캐시를 갱신한다.
void        LinModule_SetSlaveStatus(LinModule *module, const LinStatusFrame *status);
// slave가 받은 OK token을 한 번 소비한다.
uint8_t     LinModule_ConsumeSlaveOkToken(LinModule *module);

#endif
