// app 계층에서 바라보는 CAN 편의 계층이다.
// 이 모듈은 high-level request를 버퍼링하여,
// AppCore가 service 내부 구조나 timing 세부사항에 의존하지 않게 한다.
#ifndef CAN_MODULE_H
#define CAN_MODULE_H

#include "../core/infra_queue.h"
#include "can_service.h"

typedef enum
{
    CAN_MODULE_REQUEST_COMMAND = 0,
    CAN_MODULE_REQUEST_RESPONSE,
    CAN_MODULE_REQUEST_EVENT,
    CAN_MODULE_REQUEST_TEXT
} CanModuleRequestKind;

// 제출을 기다리는 app 수준 CAN 작업 항목 하나다.
// AppCore는 이 작은 request를 만들기만 하고,
// 실제 service/transport 계층으로의 유입 조절은 module에 위임한다.
typedef struct
{
    uint8_t kind;
    uint8_t target_node_id;
    uint8_t code0;
    uint8_t code1;
    uint8_t code2;
    uint8_t need_response;
    char    text[CAN_TEXT_MAX_LEN + 1U];
} CanModuleRequest;

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

// app 계층에서 바라보는 CAN module 상태다.
// 공개 request queue와,
// 실제 protocol 작업을 수행하는 내부 CAN service 객체를 함께 묶는다.
typedef struct
{
    uint8_t          initialized;
    uint8_t          local_node_id;
    uint8_t          default_target_node_id;
    uint8_t          max_submit_per_tick;
    uint8_t          last_activity;
    CanService       service;
    InfraQueue       request_queue;
    CanModuleRequest request_storage[CAN_MODULE_REQUEST_QUEUE_SIZE];
} CanModule;

// CanModule를 초기화한다.
InfraStatus CanModule_Init(CanModule *module, const CanModuleConfig *config);
// CanModule 작업을 한 번 진행한다.
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
// CanModule이 보관한 수신 메시지 하나를 꺼낸다.
uint8_t     CanModule_TryPopIncoming(CanModule *module, CanMessage *out_message);
// CanModule이 보관한 처리 결과 하나를 꺼낸다.
uint8_t     CanModule_TryPopResult(CanModule *module, CanServiceResult *out_result);

#endif
