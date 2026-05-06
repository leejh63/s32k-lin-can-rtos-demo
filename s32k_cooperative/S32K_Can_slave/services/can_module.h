// app 계층에서 바라보는 CAN 편의 계층이다.
// 이 모듈은 high-level request를 버퍼링하여,
// AppCore가 service 내부 구조나 timing 세부사항에 의존하지 않게 한다.
#ifndef CAN_MODULE_H
#define CAN_MODULE_H

#include "../core/infra_types.h"
#include "../core/can_types.h"

typedef struct CanModule CanModule;

// 시작 직후 시 선택되는 정적 CAN module 설정이다.
// module은 이 값을 사용해 node identity와 기본 timeout,
// scheduler tick마다 제출할 작업량을 정한다.
typedef struct
{
    uint8_t   local_node_id;
    uint8_t   default_target_node_id;
    uint32_t  default_timeout_ms;
    uint8_t   max_submit_per_tick;
} CanModuleConfig;

InfraStatus CanModule_Init(CanModule *module, const CanModuleConfig *config);
void        CanModule_Task(CanModule *module, uint32_t now_ms);
InfraStatus CanModule_QueueCommand(CanModule *module,
                                   uint8_t target_node_id,
                                   uint8_t command_code,
                                   uint8_t arg0,
                                   uint8_t arg1,
                                   uint8_t need_response);
InfraStatus CanModule_QueueResponse(CanModule *module,
                                    uint8_t target_node_id,
                                    uint8_t request_id,
                                    uint8_t result_code,
                                    uint8_t detail_code);
InfraStatus CanModule_QueueEvent(CanModule *module,
                                 uint8_t target_node_id,
                                 uint8_t event_code,
                                 uint8_t arg0,
                                 uint8_t arg1);
InfraStatus CanModule_QueueText(CanModule *module,
                                uint8_t target_node_id,
                                const char *text);
uint8_t     CanModule_TryPopIncoming(CanModule *module, CanMessage *out_message);
uint8_t     CanModule_TryPopResult(CanModule *module, CanServiceResult *out_result);

#endif
