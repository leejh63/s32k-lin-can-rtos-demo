// master 슬림 프로젝트 전용 설정 헤더다.
// coordinator가 실제로 쓰는 노드 식별값, task 주기,
// 콘솔 버퍼 크기만 남겨 설정 파일도 역할 중심으로 단순화한다.
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>

#define APP_NODE_ID_MASTER    1U
#define APP_NODE_ID_SLAVE1    2U

#define APP_TASK_UART_MS          1U
#define APP_TASK_LIN_FAST_MS      1U
#define APP_TASK_CAN_MS           10U
#define APP_TASK_LIN_POLL_MS      20U
#define APP_TASK_RENDER_MS        100U
#define APP_TASK_HEARTBEAT_MS     1000U

#define APP_LIN_STATUS_MAX_AGE_MS           100U
#define APP_MASTER_OK_RETRY_INTERVAL_MS      40U
#define APP_MASTER_OK_TIMEOUT_MS            400U
#define APP_MASTER_OK_MAX_RETRY_COUNT         8U

#define APP_CAN_MAX_CONSOLE_COMMANDS_PER_TICK 4U
#define APP_CAN_MAX_RESULTS_PER_TICK          4U
#define APP_CAN_MAX_INCOMING_PER_TICK         4U

#define APP_CONSOLE_PROFILE_PUTTY 1U
#define APP_CONSOLE_PROFILE_GUI   2U

// primary UART 출력 형식을 빌드 타임에 선택한다.
// PUTTY profile: 기존 ANSI terminal UI
// GUI profile: line-based plain text snapshot
#define APP_CONSOLE_OUTPUT_PROFILE APP_CONSOLE_PROFILE_GUI

#define APP_CONSOLE_LINE_BUFFER_SIZE   64U
#define APP_CONSOLE_INPUT_VIEW_SIZE    64U
#define APP_CONSOLE_TASK_VIEW_SIZE     160U
#define APP_CONSOLE_SOURCE_VIEW_SIZE   128U
#define APP_CONSOLE_RESULT_VIEW_SIZE   96U
#define APP_CONSOLE_VALUE_VIEW_SIZE    128U
#define APP_CONSOLE_CAN_CMD_QUEUE_SIZE 4U

#endif
