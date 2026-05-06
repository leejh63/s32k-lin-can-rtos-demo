// 프로젝트 전체가 공유하는 CAN 공통 타입과 상수 정의다.
// app, service, driver가 같은 메시지/에러 표현을 공유하되,
// 특정 계층 소속처럼 보이지 않도록 core에 둔다.
#ifndef CAN_TYPES_H
#define CAN_TYPES_H

#include <stdint.h>

#define CAN_NODE_ID_INVALID           0U
#define CAN_NODE_ID_MIN               1U
#define CAN_NODE_ID_MAX               254U
#define CAN_NODE_ID_BROADCAST         255U

#define CAN_FRAME_DATA_SIZE           16U
#define CAN_TEXT_MAX_LEN              11U
#define CAN_TEXT_FRAME_HEADER_SIZE    5U

#define CAN_HW_COMMAND_TX_MAILBOX_INDEX     1U
#define CAN_HW_ACCEPT_ALL_RX_MAILBOX_INDEX  0U
#define CAN_HW_RX_QUEUE_SIZE          8U
#define CAN_TRANSPORT_TX_QUEUE_SIZE   8U
#define CAN_TRANSPORT_RX_QUEUE_SIZE   8U
#define CAN_SERVICE_QUEUE_SIZE        8U
#define CAN_SERVICE_PENDING_SIZE      4U
#define CAN_MODULE_REQUEST_QUEUE_SIZE 8U

#define CAN_PROTO_VERSION_V1          1U
#define CAN_PROTO_STDID_COMMAND       0x120U
#define CAN_PROTO_STDID_RESPONSE      0x121U
#define CAN_PROTO_STDID_EVENT         0x122U
#define CAN_PROTO_STDID_TEXT          0x123U

#define CAN_MSG_FLAG_NEED_RESPONSE    (1U << 0)
#define CAN_MSG_FLAG_ERROR            (1U << 1)

typedef enum
{
    CAN_MSG_COMMAND = 0,
    CAN_MSG_RESPONSE,
    CAN_MSG_EVENT,
    CAN_MSG_TEXT
} CanMessageType;

typedef enum
{
    CAN_PAYLOAD_NONE = 0,
    CAN_PAYLOAD_CTRL_CMD,
    CAN_PAYLOAD_CTRL_RESULT,
    CAN_PAYLOAD_EVENT_DATA,
    CAN_PAYLOAD_TEXT_DATA
} CanPayloadKind;

typedef enum
{
    CAN_CMD_NONE = 0,
    CAN_CMD_OPEN,
    CAN_CMD_CLOSE,
    CAN_CMD_OFF,
    CAN_CMD_TEST,
    CAN_CMD_OK,
    CAN_CMD_EMERGENCY,
    CAN_CMD_STATUS_REQ
} CanCommandCode;

typedef enum
{
    CAN_RES_NONE = 0,
    CAN_RES_OK,
    CAN_RES_FAIL,
    CAN_RES_INVALID_TARGET,
    CAN_RES_BUSY,
    CAN_RES_TIMEOUT,
    CAN_RES_NOT_SUPPORTED
} CanResultCode;

typedef enum
{
    CAN_EVENT_NONE = 0,
    CAN_EVENT_BOOT,
    CAN_EVENT_ONLINE,
    CAN_EVENT_OFFLINE,
    CAN_EVENT_HEARTBEAT,
    CAN_EVENT_WARNING,
    CAN_EVENT_ERROR
} CanEventCode;

typedef enum
{
    CAN_TEXT_NONE = 0,
    CAN_TEXT_USER,
    CAN_TEXT_LOG,
    CAN_TEXT_DEBUG,
    CAN_TEXT_RESPONSE,
    CAN_TEXT_EVENT
} CanTextType;

typedef enum
{
    CAN_PROTO_DECODE_OK = 0,
    CAN_PROTO_DECODE_IGNORED,
    CAN_PROTO_DECODE_INVALID,
    CAN_PROTO_DECODE_UNSUPPORTED
} CanProtoDecodeStatus;

typedef enum
{
    CAN_HW_ERROR_NONE = 0,
    CAN_HW_ERROR_NOT_READY,
    CAN_HW_ERROR_INIT_FAIL,
    CAN_HW_ERROR_RX_CONFIG_FAIL,
    CAN_HW_ERROR_TX_CONFIG_FAIL,
    CAN_HW_ERROR_TX_START_FAIL,
    CAN_HW_ERROR_TX_STATUS_FAIL,
    CAN_HW_ERROR_RX_STATUS_FAIL,
    CAN_HW_ERROR_RX_QUEUE_FULL,
    CAN_HW_ERROR_RX_RESTART_FAIL
} CanHwError;

typedef enum
{
    CAN_TRANSPORT_ERROR_NONE = 0,
    CAN_TRANSPORT_ERROR_NOT_READY,
    CAN_TRANSPORT_ERROR_TX_QUEUE_FULL,
    CAN_TRANSPORT_ERROR_RX_QUEUE_FULL,
    CAN_TRANSPORT_ERROR_HW_TX_FAIL,
    CAN_TRANSPORT_ERROR_HW_RX_FAIL
} CanTransportError;

typedef enum
{
    CAN_SERVICE_ERROR_NONE = 0,
    CAN_SERVICE_ERROR_NOT_READY,
    CAN_SERVICE_ERROR_INVALID_TARGET,
    CAN_SERVICE_ERROR_PENDING_FULL,
    CAN_SERVICE_ERROR_TX_QUEUE_FULL,
    CAN_SERVICE_ERROR_RX_QUEUE_FULL,
    CAN_SERVICE_ERROR_RESULT_QUEUE_FULL,
    CAN_SERVICE_ERROR_PROTOCOL_ERROR,
    CAN_SERVICE_ERROR_UNSUPPORTED
} CanServiceError;

typedef enum
{
    CAN_SERVICE_RESULT_NONE = 0,
    CAN_SERVICE_RESULT_RESPONSE,
    CAN_SERVICE_RESULT_TIMEOUT,
    CAN_SERVICE_RESULT_SEND_FAIL
} CanServiceResultKind;

// transport 계층이 주고받는 raw CAN frame 컨테이너다.
// 시간 기준은 frame에 싣지 않고, service/task 호출 시점에서 따로 관리한다.
typedef struct
{
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[CAN_FRAME_DATA_SIZE];
    uint8_t  is_extended_id;
    uint8_t  is_remote_frame;
} CanFrame;

// app과 service 계층이 이해하는 논리적 CAN 메시지다.
// protocol encoder는 이 구조를 frame으로 바꾸고,
// 수신 경로에서는 다시 typed message로 복원한다.
typedef struct
{
    uint8_t  version;
    uint8_t  message_type;
    uint8_t  source_node_id;
    uint8_t  target_node_id;
    uint8_t  request_id;
    uint8_t  flags;
    uint8_t  payload_kind;
    uint8_t  payload_length;
    uint8_t  payload[CAN_FRAME_DATA_SIZE];
    uint8_t  text_type;
    uint8_t  text_length;
    char     text[CAN_TEXT_MAX_LEN + 1U];
} CanMessage;

// 추적 중이던 CAN 요청이 끝났을 때 생성되는 결과 기록이다.
// app은 response와 timeout, 전송 실패를 이 구조 하나로 진단한다.
typedef struct
{
    uint8_t kind;
    uint8_t request_id;
    uint8_t source_node_id;
    uint8_t target_node_id;
    uint8_t command_code;
    uint8_t result_code;
    uint8_t detail_code;
} CanServiceResult;

#endif
