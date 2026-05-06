#ifndef APP_CONSOLE_INTERNAL_H
#define APP_CONSOLE_INTERNAL_H

#include "app_console.h"
#include "app_config.h"
#include "../core/infra_queue.h"
#include "../services/uart_service_internal.h"

typedef enum
{
    APP_CONSOLE_STATE_IDLE = 0,
    APP_CONSOLE_STATE_ERROR
} AppConsoleState;

typedef struct
{
    char    input_text[APP_CONSOLE_INPUT_VIEW_SIZE];
    char    task_text[APP_CONSOLE_TASK_VIEW_SIZE];
    char    source_text[APP_CONSOLE_SOURCE_VIEW_SIZE];
    char    result_text[APP_CONSOLE_RESULT_VIEW_SIZE];
    char    value_text[APP_CONSOLE_VALUE_VIEW_SIZE];
    uint8_t full_refresh_required;
    uint8_t input_dirty;
    uint8_t task_dirty;
    uint8_t source_dirty;
    uint8_t result_dirty;
    uint8_t value_dirty;
    uint8_t layout_drawn;
} AppConsoleView;

struct AppConsole
{
    uint8_t              initialized;
    uint8_t              node_id;
    AppConsoleState      state;
    uint8_t              local_ok_pending;
    UartService          uart;
    InfraQueue           can_cmd_queue;
    AppConsoleCanCommand can_cmd_storage[APP_CONSOLE_CAN_CMD_QUEUE_SIZE];
    AppConsoleView       view;
};

#endif
