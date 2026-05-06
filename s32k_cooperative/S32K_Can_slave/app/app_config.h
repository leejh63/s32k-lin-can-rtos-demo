// CAN 현장 반응 slave 최소 운영 구성용 설정 헤더다.
// slave1이 실제 동작에 쓰는 peer node id, 버튼/LED/CAN 주기,
// 그리고 승인 blink 횟수만 남겨 설정도 역할 중심으로 줄인다.
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_NODE_ID_MASTER    1U
#define APP_NODE_ID_SLAVE1    2U

#define APP_TASK_BUTTON_MS        10U
#define APP_TASK_CAN_MS           10U
#define APP_TASK_LED_MS           100U
#define APP_TASK_HEARTBEAT_MS     1000U

#define APP_CAN_DEFAULT_TIMEOUT_MS    300U
#define APP_CAN_MAX_SUBMIT_PER_TICK   2U
#define APP_CAN_MAX_RESULTS_PER_TICK  4U
#define APP_CAN_MAX_INCOMING_PER_TICK 4U

#define APP_SLAVE1_ACK_TOGGLES    6U

#endif
