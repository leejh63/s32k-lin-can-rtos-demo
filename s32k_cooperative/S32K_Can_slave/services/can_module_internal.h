// CAN module 내부 상태 선언이다.
// app 쪽에는 작은 request API만 노출하고,
// queue 저장소와 하위 service ownership은 이 헤더에만 둔다.
#ifndef CAN_MODULE_INTERNAL_H
#define CAN_MODULE_INTERNAL_H

#include "can_module.h"

#include "../core/infra_queue.h"
#include "can_service_internal.h"

typedef enum
{
    CAN_MODULE_REQUEST_COMMAND = 0,
    CAN_MODULE_REQUEST_RESPONSE,
    CAN_MODULE_REQUEST_EVENT,
    CAN_MODULE_REQUEST_TEXT
} CanModuleRequestKind;

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

struct CanModule
{
    uint8_t          initialized;
    uint8_t          local_node_id;
    uint8_t          default_target_node_id;
    uint8_t          max_submit_per_tick;
    uint8_t          last_activity;
    CanService       service;
    InfraQueue       request_queue;
    CanModuleRequest request_storage[CAN_MODULE_REQUEST_QUEUE_SIZE];
};

#endif
