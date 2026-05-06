// 사람이 읽을 수 있는 제어와 상태 표시용 UART 콘솔 인터페이스다.
// 콘솔은 화면 렌더 상태를 유지하고 명령을 파싱하며,
// queue에 쌓인 CAN 요청을 app 계층에 노출한다.
#ifndef APP_CONSOLE_H
#define APP_CONSOLE_H

#include <stdint.h>

#include "../core/infra_types.h"
#include "../services/can_types.h"

typedef enum
{
    APP_CONSOLE_CAN_CMD_NONE = 0,
    APP_CONSOLE_CAN_CMD_OPEN,
    APP_CONSOLE_CAN_CMD_CLOSE,
    APP_CONSOLE_CAN_CMD_OFF,
    APP_CONSOLE_CAN_CMD_TEST,
    APP_CONSOLE_CAN_CMD_TEXT,
    APP_CONSOLE_CAN_CMD_EVENT
} AppConsoleCanCommandType;

// 파싱된 CAN 지향 콘솔 명령 구조체다.
// 콘솔은 operator 의도를 이 중립적인 형태로 저장해 두고,
// AppCore가 추후 CAN 모듈 요청으로 변환한다.
typedef struct
{
    uint8_t type;
    uint8_t target_node_id;
    uint8_t target_is_broadcast;
    char    text[CAN_TEXT_MAX_LEN + 1U];
    uint8_t event_code;
    uint8_t arg0;
    uint8_t arg1;
} AppConsoleCanCommand;

typedef struct AppConsole AppConsole;

InfraStatus AppConsole_Init(AppConsole *console, uint8_t node_id);
void        AppConsole_Task(AppConsole *console, uint32_t now_ms);
void        AppConsole_Render(AppConsole *console);
uint8_t     AppConsole_TryPopCanCommand(AppConsole *console, AppConsoleCanCommand *out_cmd);
uint8_t     AppConsole_ConsumeLocalOk(AppConsole *console);
void        AppConsole_QueueLocalOk(AppConsole *console);
void        AppConsole_SetTaskText(AppConsole *console, const char *text);
void        AppConsole_SetSourceText(AppConsole *console, const char *text);
void        AppConsole_SetResultText(AppConsole *console, const char *text);
void        AppConsole_SetValueText(AppConsole *console, const char *text);
void        AppConsole_CopyResultLine(const AppConsole *console, char *out_buffer, uint16_t out_buffer_size);
void        AppConsole_CopyValueLine(const AppConsole *console,
                                     uint8_t line_index,
                                     char *out_buffer,
                                     uint16_t out_buffer_size);
uint8_t     AppConsole_IsError(const AppConsole *console);

#endif
