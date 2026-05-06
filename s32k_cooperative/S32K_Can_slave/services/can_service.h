// CAN request/response service 인터페이스다.
// 이 계층은 raw transport와 protocol 모듈 위에,
// request 추적과 timeout 처리, 메시지 queue를 추가한다.
#ifndef CAN_SERVICE_H
#define CAN_SERVICE_H

#include "../core/infra_types.h"
#include "../core/can_types.h"

typedef struct CanService CanService;

uint8_t CanService_Init(CanService *service,
                        uint8_t local_node_id,
                        uint32_t default_timeout_ms);
void    CanService_Task(CanService *service, uint32_t now_ms);
void    CanService_FlushTx(CanService *service, uint32_t now_ms);
uint8_t CanService_SendCommand(CanService *service,
                               uint8_t target_node_id,
                               uint8_t command_code,
                               uint8_t arg0,
                               uint8_t arg1,
                               uint8_t need_response);
uint8_t CanService_SendResponse(CanService *service,
                                uint8_t target_node_id,
                                uint8_t request_id,
                                uint8_t result_code,
                                uint8_t detail_code);
uint8_t CanService_SendEvent(CanService *service,
                             uint8_t target_node_id,
                             uint8_t event_code,
                             uint8_t arg0,
                             uint8_t arg1);
uint8_t CanService_SendText(CanService *service,
                            uint8_t target_node_id,
                            uint8_t text_type,
                            const char *text);
uint8_t CanService_PopReceivedMessage(CanService *service, CanMessage *out_message);
uint8_t CanService_PopResult(CanService *service, CanServiceResult *out_result);

#endif
