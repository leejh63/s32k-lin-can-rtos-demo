#include "app_ble_bridge.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../platform/s32k_sdk/isosdk_uart.h"

static void AppBleBridge_SendText(AppBleBridge *bridge, const char *text)
{
    if ((bridge == NULL) || (text == NULL) || (bridge->enabled == 0U))
    {
        return;
    }

    (void)UartService_RequestTx(&bridge->uart, text);
}

static void AppBleBridge_SendLine(AppBleBridge *bridge, const char *text)
{
    char buffer[APP_CONSOLE_VALUE_VIEW_SIZE + APP_CONSOLE_RESULT_VIEW_SIZE];

    if ((bridge == NULL) || (text == NULL))
    {
        return;
    }

    (void)snprintf(buffer, sizeof(buffer), "%s\r\n", text);
    AppBleBridge_SendText(bridge, buffer);
}

static void AppBleBridge_SendStatusSnapshot(AppBleBridge *bridge, const AppConsole *console)
{
    char buffer[APP_CONSOLE_RESULT_VIEW_SIZE + 16U];
    char line0[APP_CONSOLE_RESULT_VIEW_SIZE];
    char line1[APP_CONSOLE_RESULT_VIEW_SIZE];
    char line2[APP_CONSOLE_RESULT_VIEW_SIZE];
    char result_line[APP_CONSOLE_RESULT_VIEW_SIZE];

    if ((bridge == NULL) || (console == NULL))
    {
        return;
    }

    AppConsole_CopyValueLine(console, 0U, line0, (uint16_t)sizeof(line0));
    AppConsole_CopyValueLine(console, 1U, line1, (uint16_t)sizeof(line1));
    AppConsole_CopyValueLine(console, 2U, line2, (uint16_t)sizeof(line2));
    AppConsole_CopyResultLine(console, result_line, (uint16_t)sizeof(result_line));

    if (line0[0] != '\0')
    {
        AppBleBridge_SendLine(bridge, line0);
    }

    if (line1[0] != '\0')
    {
        AppBleBridge_SendLine(bridge, line1);
    }

    if (line2[0] != '\0')
    {
        AppBleBridge_SendLine(bridge, line2);
    }

    (void)snprintf(buffer, sizeof(buffer), "Result : %s", result_line);
    AppBleBridge_SendLine(bridge, buffer);
}

static void AppBleBridge_HandleLine(AppBleBridge *bridge, AppConsole *console, const char *line)
{
    if ((bridge == NULL) || (console == NULL) || (line == NULL))
    {
        return;
    }

    if (strcmp(line, "ok") == 0)
    {
        AppConsole_QueueLocalOk(console);
        return;
    }

    if ((strcmp(line, "status") == 0) || (strcmp(line, "s") == 0))
    {
        AppBleBridge_SendStatusSnapshot(bridge, console);
        return;
    }

    if ((strcmp(line, "help") == 0) || (strcmp(line, "h") == 0))
    {
        AppBleBridge_SendLine(bridge, "ble commands: ok, status");
    }
}

InfraStatus AppBleBridge_Init(AppBleBridge *bridge)
{
    if (bridge == NULL)
    {
        return INFRA_STATUS_INVALID_ARG;
    }

    (void)memset(bridge, 0, sizeof(*bridge));
    if (IsoSdk_UartIsSecondarySupported() == 0U)
    {
        return INFRA_STATUS_UNSUPPORTED;
    }

    if (UartService_InitWithInstance(&bridge->uart,
                                     IsoSdk_UartGetSecondaryInstance()) != INFRA_STATUS_OK)
    {
        return INFRA_STATUS_IO_ERROR;
    }

    bridge->enabled = 1U;
    return INFRA_STATUS_OK;
}

void AppBleBridge_Task(AppBleBridge *bridge, AppConsole *console, uint32_t now_ms)
{
    char line_buffer[APP_CONSOLE_LINE_BUFFER_SIZE];

    if ((bridge == NULL) || (console == NULL) || (bridge->enabled == 0U))
    {
        return;
    }

    UartService_ProcessRx(&bridge->uart);
    if (UartService_HasError(&bridge->uart) != 0U)
    {
        if (UartService_Recover(&bridge->uart) != INFRA_STATUS_OK)
        {
            bridge->enabled = 0U;
            return;
        }
    }

    if (UartService_HasLine(&bridge->uart) != 0U)
    {
        if (UartService_ReadLine(&bridge->uart,
                                 line_buffer,
                                 (uint16_t)sizeof(line_buffer)) == INFRA_STATUS_OK)
        {
            AppBleBridge_HandleLine(bridge, console, line_buffer);
        }
    }

    UartService_ProcessTx(&bridge->uart, now_ms);
}

void AppBleBridge_Sync(AppBleBridge *bridge, const AppConsole *console)
{
    char result_line[APP_CONSOLE_RESULT_VIEW_SIZE];

    if ((bridge == NULL) || (console == NULL) || (bridge->enabled == 0U))
    {
        return;
    }

    AppConsole_CopyResultLine(console, result_line, (uint16_t)sizeof(result_line));
    if (strncmp(result_line,
                bridge->last_result_line,
                sizeof(bridge->last_result_line)) == 0)
    {
        return;
    }

    (void)snprintf(bridge->last_result_line,
                   sizeof(bridge->last_result_line),
                   "%s",
                   result_line);
    if (result_line[0] != '\0')
    {
        AppBleBridge_SendLine(bridge, result_line);
    }
}
