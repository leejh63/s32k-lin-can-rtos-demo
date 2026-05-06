// CAN 메시지 encode/decode 인터페이스다.
// protocol 계층은 논리 메시지를 raw frame으로 바꿔,
// 나머지 시스템이 typed payload와 node ID 기준으로 동작하게 한다.
#ifndef CAN_PROTO_H
#define CAN_PROTO_H

#include "can_types.h"

// CAN protocol codec의 정적 설정 구조체다.
// codec은 로컬 node identity를 유지하여,
// 이후 protocol 변화가 생겨도 app 세부사항을 노출하지 않게 한다.
typedef struct
{
    uint8_t local_node_id;
} CanProtoConfig;

typedef struct
{
    CanFrame frames[1];
    uint8_t  count;
} CanEncodedFrameList;

// CAN protocol encoder/decoder의 런타임 상태다.
// 설정 정보뿐 아니라 decode 통계도 함께 기록하여,
// 잘못된 frame이나 unsupported frame을 진단할 때 도움이 된다.
typedef struct
{
    uint8_t  initialized;
    uint8_t  local_node_id;
    uint32_t decode_ok_count;
    uint32_t decode_ignored_count;
    uint32_t decode_invalid_count;
    uint32_t decode_unsupported_count;
} CanProto;

// CanProto를 초기화한다.
uint8_t              CanProto_Init(CanProto *proto, const CanProtoConfig *config);
uint8_t              CanProto_EncodeMessage(CanProto *proto,
                                            const CanMessage *message,
                                            CanEncodedFrameList *out_list);
CanProtoDecodeStatus CanProto_DecodeFrame(CanProto *proto,
                                          const CanFrame *frame,
                                          CanMessage *out_message);

#endif
