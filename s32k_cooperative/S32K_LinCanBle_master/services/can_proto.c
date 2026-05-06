// CAN 프로토콜 encoder/decoder 구현 파일이다.
// 내부 메시지와 실제 CAN frame 사이를 변환하기 전에,
// node ID와 payload layout을 먼저 검증한다.
#include "can_proto.h"

#include <stddef.h>
#include <string.h>

// raw CAN frame 컨테이너 하나를 0으로 초기화한다.
// 이런 보조 함수를 두면 encode/decode 경로에서,
// 필드를 채우기 전에 깨끗한 목적지 객체를 쉽게 준비할 수 있다.
static void CanProto_ClearFrame(CanFrame *frame)
{
    if (frame != NULL)
    {
        (void)memset(frame, 0, sizeof(*frame));
    }
}

// 논리 CAN 메시지 저장 공간을 기본 상태로 비운다.
static void CanProto_ClearMessage(CanMessage *message)
{
    if (message != NULL)
    {
        (void)memset(message, 0, sizeof(*message));
    }
}

// protocol에서 허용하는 node id 범위인지 확인한다.
static uint8_t CanProto_IsValidNodeId(uint8_t node_id)
{
    if (node_id == CAN_NODE_ID_BROADCAST)
    {
        return 1U;
    }

    return ((node_id >= CAN_NODE_ID_MIN) && (node_id <= CAN_NODE_ID_MAX)) ? 1U : 0U;
}

// text payload가 사람이 읽을 수 있는 ASCII 범위인지 검사한다.
static uint8_t CanProto_IsPrintableAscii(const char *text, uint8_t length)
{
    uint8_t       index;
    unsigned char ch;

    if (text == NULL)
    {
        return 0U;
    }

    for (index = 0U; index < length; index++)
    {
        ch = (unsigned char)text[index];
        if ((ch < 32U) || (ch > 126U))
        {
            return 0U;
        }
    }

    return 1U;
}

// 논리 메시지 종류를 wire standard ID로 바꾼다.
static uint32_t CanProto_MessageTypeToId(uint8_t message_type)
{
    switch (message_type)
    {
        case CAN_MSG_COMMAND:
            return CAN_PROTO_STDID_COMMAND;

        case CAN_MSG_RESPONSE:
            return CAN_PROTO_STDID_RESPONSE;

        case CAN_MSG_EVENT:
            return CAN_PROTO_STDID_EVENT;

        case CAN_MSG_TEXT:
            return CAN_PROTO_STDID_TEXT;

        default:
            return 0U;
    }
}

// 수신 frame ID를 논리 메시지 종류로 역변환한다.
static uint8_t CanProto_IdToMessageType(uint32_t id, uint8_t *out_message_type)
{
    if (out_message_type == NULL)
    {
        return 0U;
    }

    switch (id)
    {
        case CAN_PROTO_STDID_COMMAND:
            *out_message_type = CAN_MSG_COMMAND;
            return 1U;

        case CAN_PROTO_STDID_RESPONSE:
            *out_message_type = CAN_MSG_RESPONSE;
            return 1U;

        case CAN_PROTO_STDID_EVENT:
            *out_message_type = CAN_MSG_EVENT;
            return 1U;

        case CAN_PROTO_STDID_TEXT:
            *out_message_type = CAN_MSG_TEXT;
            return 1U;

        default:
            return 0U;
    }
}

// 로컬 node identity로 protocol codec을 초기화한다.
// codec은 호출자 설정을 복사하는 동시에,
// 작은 decode 통계 집합도 함께 추적한다.
uint8_t CanProto_Init(CanProto *proto, const CanProtoConfig *config)
{
    if ((proto == NULL) || (config == NULL))
    {
        return 0U;
    }

    (void)memset(proto, 0, sizeof(*proto));
    proto->local_node_id = config->local_node_id;
    proto->initialized = 1U;
    return 1U;
}

// 논리 메시지 하나를 하나 이상의 CAN frame으로 encode한다.
// version과 node ID, payload 길이, text 제약을 여기서 검증한 뒤,
// transport 계층에 frame을 넘긴다.
uint8_t CanProto_EncodeMessage(CanProto *proto,
                               const CanMessage *message,
                               CanEncodedFrameList *out_list)
{
    CanFrame *frame;
    uint8_t   text_length;
    uint32_t  frame_id;

    if ((proto == NULL) || (message == NULL) || (out_list == NULL) || (proto->initialized == 0U))
    {
        return 0U;
    }

    (void)memset(out_list, 0, sizeof(*out_list));

    if ((message->version != CAN_PROTO_VERSION_V1) ||
        (CanProto_IsValidNodeId(message->source_node_id) == 0U) ||
        (CanProto_IsValidNodeId(message->target_node_id) == 0U))
    {
        return 0U;
    }

    frame_id = CanProto_MessageTypeToId(message->message_type);
    if (frame_id == 0U)
    {
        return 0U;
    }

    frame = &out_list->frames[0];
    CanProto_ClearFrame(frame);
    frame->id = frame_id;

    if (message->message_type == CAN_MSG_TEXT)
    {
        text_length = message->text_length;
        if ((text_length == 0U) || (text_length > CAN_TEXT_MAX_LEN))
        {
            return 0U;
        }

        if ((uint8_t)(CAN_TEXT_FRAME_HEADER_SIZE + text_length) > CAN_FRAME_DATA_SIZE)
        {
            return 0U;
        }

        if (CanProto_IsPrintableAscii(message->text, text_length) == 0U)
        {
            return 0U;
        }

        frame->dlc = (uint8_t)(CAN_TEXT_FRAME_HEADER_SIZE + text_length);
        frame->data[0] = message->version;
        frame->data[1] = message->source_node_id;
        frame->data[2] = message->target_node_id;
        frame->data[3] = message->text_type;
        frame->data[4] = text_length;
        (void)memcpy(&frame->data[5], message->text, text_length);
        out_list->count = 1U;
        return 1U;
    }

    frame->dlc = 8U;
    frame->data[0] = message->version;
    frame->data[1] = message->source_node_id;
    frame->data[2] = message->target_node_id;
    frame->data[3] = message->request_id;
    frame->data[4] = (message->payload_length > 0U) ? message->payload[0] : 0U;
    frame->data[5] = (message->payload_length > 1U) ? message->payload[1] : 0U;
    frame->data[6] = (message->payload_length > 2U) ? message->payload[2] : 0U;
    frame->data[7] = message->flags;
    out_list->count = 1U;
    return 1U;
}

// 수신한 raw frame 하나를 논리 메시지로 decode한다.
// unsupported ID와 malformed payload는 여기서 걸러서,
// 상위 계층이 검증된 protocol 데이터만 처리하도록 한다.
CanProtoDecodeStatus CanProto_DecodeFrame(CanProto *proto,
                                          const CanFrame *frame,
                                          CanMessage *out_message)
{
    uint8_t message_type;
    uint8_t text_length;

    if ((proto == NULL) || (frame == NULL) || (out_message == NULL) || (proto->initialized == 0U))
    {
        return CAN_PROTO_DECODE_INVALID;
    }

    CanProto_ClearMessage(out_message);

    if ((frame->is_extended_id != 0U) || (frame->is_remote_frame != 0U))
    {
        proto->decode_unsupported_count++;
        return CAN_PROTO_DECODE_UNSUPPORTED;
    }

    if (CanProto_IdToMessageType(frame->id, &message_type) == 0U)
    {
        proto->decode_ignored_count++;
        return CAN_PROTO_DECODE_IGNORED;
    }

    if (message_type == CAN_MSG_TEXT)
    {
        if ((frame->dlc < CAN_TEXT_FRAME_HEADER_SIZE) || (frame->data[0] != CAN_PROTO_VERSION_V1))
        {
            proto->decode_invalid_count++;
            return CAN_PROTO_DECODE_INVALID;
        }

        out_message->version = frame->data[0];
        out_message->message_type = message_type;
        out_message->source_node_id = frame->data[1];
        out_message->target_node_id = frame->data[2];
        out_message->text_type = frame->data[3];
        out_message->text_length = frame->data[4];
        out_message->payload_kind = CAN_PAYLOAD_TEXT_DATA;

        if ((CanProto_IsValidNodeId(out_message->source_node_id) == 0U) ||
            (CanProto_IsValidNodeId(out_message->target_node_id) == 0U))
        {
            proto->decode_invalid_count++;
            return CAN_PROTO_DECODE_INVALID;
        }

        text_length = out_message->text_length;
        if ((text_length == 0U) || (text_length > CAN_TEXT_MAX_LEN))
        {
            proto->decode_invalid_count++;
            return CAN_PROTO_DECODE_INVALID;
        }

        if ((uint8_t)(CAN_TEXT_FRAME_HEADER_SIZE + text_length) > frame->dlc)
        {
            proto->decode_invalid_count++;
            return CAN_PROTO_DECODE_INVALID;
        }

        (void)memcpy(out_message->text, &frame->data[5], text_length);
        out_message->text[text_length] = '\0';

        if (CanProto_IsPrintableAscii(out_message->text, text_length) == 0U)
        {
            proto->decode_invalid_count++;
            return CAN_PROTO_DECODE_INVALID;
        }

        proto->decode_ok_count++;
        return CAN_PROTO_DECODE_OK;
    }

    if ((frame->dlc < 8U) || (frame->data[0] != CAN_PROTO_VERSION_V1))
    {
        proto->decode_invalid_count++;
        return CAN_PROTO_DECODE_INVALID;
    }

    out_message->version = frame->data[0];
    out_message->message_type = message_type;
    out_message->source_node_id = frame->data[1];
    out_message->target_node_id = frame->data[2];
    out_message->request_id = frame->data[3];
    out_message->flags = frame->data[7];

    if ((CanProto_IsValidNodeId(out_message->source_node_id) == 0U) ||
        (CanProto_IsValidNodeId(out_message->target_node_id) == 0U))
    {
        proto->decode_invalid_count++;
        return CAN_PROTO_DECODE_INVALID;
    }

    switch (message_type)
    {
        case CAN_MSG_COMMAND:
            out_message->payload_kind = CAN_PAYLOAD_CTRL_CMD;
            out_message->payload_length = 3U;
            break;

        case CAN_MSG_RESPONSE:
            out_message->payload_kind = CAN_PAYLOAD_CTRL_RESULT;
            out_message->payload_length = 3U;
            break;

        case CAN_MSG_EVENT:
            out_message->payload_kind = CAN_PAYLOAD_EVENT_DATA;
            out_message->payload_length = 3U;
            break;

        default:
            proto->decode_invalid_count++;
            return CAN_PROTO_DECODE_INVALID;
    }

    out_message->payload[0] = frame->data[4];
    out_message->payload[1] = frame->data[5];
    out_message->payload[2] = frame->data[6];
    proto->decode_ok_count++;
    return CAN_PROTO_DECODE_OK;
}
