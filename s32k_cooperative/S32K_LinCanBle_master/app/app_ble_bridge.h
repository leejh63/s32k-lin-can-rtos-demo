#ifndef APP_BLE_BRIDGE_H
#define APP_BLE_BRIDGE_H

#include "app_config.h"
#include "app_console.h"
#include "../services/uart_service_internal.h"

typedef struct
{
    uint8_t     enabled;
    UartService uart;
    char        last_result_line[APP_CONSOLE_RESULT_VIEW_SIZE];
} AppBleBridge;

InfraStatus AppBleBridge_Init(AppBleBridge *bridge);
void        AppBleBridge_Task(AppBleBridge *bridge, AppConsole *console, uint32_t now_ms);
void        AppBleBridge_Sync(AppBleBridge *bridge, const AppConsole *console);

#endif
