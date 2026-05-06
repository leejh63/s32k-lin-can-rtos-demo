#ifndef APP_CONSOLE_OUTPUT_H
#define APP_CONSOLE_OUTPUT_H

#include "app_console_internal.h"

#if ((APP_CONSOLE_OUTPUT_PROFILE != APP_CONSOLE_PROFILE_PUTTY) && \
     (APP_CONSOLE_OUTPUT_PROFILE != APP_CONSOLE_PROFILE_GUI))
#error "APP_CONSOLE_OUTPUT_PROFILE must be APP_CONSOLE_PROFILE_PUTTY or APP_CONSOLE_PROFILE_GUI"
#endif

static uint8_t AppConsoleOutput_HasGuiDirty(const AppConsole *console)
{
    if (console == NULL)
    {
        return 0U;
    }

    return ((console->view.task_dirty != 0U) ||
            (console->view.source_dirty != 0U) ||
            (console->view.result_dirty != 0U) ||
            (console->view.value_dirty != 0U)) ? 1U : 0U;
}

static void AppConsoleOutput_ClearDirty(AppConsole *console)
{
    if (console == NULL)
    {
        return;
    }

    console->view.input_dirty = 0U;
    console->view.task_dirty = 0U;
    console->view.source_dirty = 0U;
    console->view.result_dirty = 0U;
    console->view.value_dirty = 0U;
}

static void AppConsoleOutput_RenderGuiSnapshot(AppConsole *console)
{
    char        render_buffer[APP_CONSOLE_RENDER_BUFFER_SIZE];
    char        task_line1[64];
    char        task_line2[64];
    char        task_line3[64];
    char        task_line4[64];
    char        source_line1[64];
    char        source_line2[64];
    char        value_line1[64];
    char        value_line2[64];
    char        value_line3[64];
    char        result_line[APP_CONSOLE_RESULT_VIEW_SIZE];
    const char *title_text;

    if (console == NULL)
    {
        return;
    }

    if (AppConsole_IsUartBackpressured(console) != 0U)
    {
        return;
    }

    title_text = (console->node_id == APP_NODE_ID_MASTER) ? "MASTER NODE" : "FIELD NODE";
    AppConsole_ExtractTextLine(console->view.task_text, 0U, task_line1, (uint16_t)sizeof(task_line1));
    AppConsole_ExtractTextLine(console->view.task_text, 1U, task_line2, (uint16_t)sizeof(task_line2));
    AppConsole_ExtractTextLine(console->view.task_text, 2U, task_line3, (uint16_t)sizeof(task_line3));
    AppConsole_ExtractTextLine(console->view.task_text, 3U, task_line4, (uint16_t)sizeof(task_line4));
    AppConsole_ExtractTextLine(console->view.source_text, 0U, source_line1, (uint16_t)sizeof(source_line1));
    AppConsole_ExtractTextLine(console->view.source_text, 1U, source_line2, (uint16_t)sizeof(source_line2));
    AppConsole_ExtractTextLine(console->view.value_text, 0U, value_line1, (uint16_t)sizeof(value_line1));
    AppConsole_ExtractTextLine(console->view.value_text, 1U, value_line2, (uint16_t)sizeof(value_line2));
    AppConsole_ExtractTextLine(console->view.value_text, 2U, value_line3, (uint16_t)sizeof(value_line3));
    AppConsole_CopyResultLine(console, result_line, (uint16_t)sizeof(result_line));

    if (result_line[0] == '\0')
    {
        (void)snprintf(result_line, sizeof(result_line), "waiting");
    }

    (void)snprintf(render_buffer,
                   sizeof(render_buffer),
                   "[ %s ]\r\n"
                   "\r\n"
                   "[Connection Status]\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "[Status]\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "[Input]\r\n"
                   "%s\r\n"
                   "%s\r\n"
                   "[Message]\r\n"
                   "%s\r\n",
                   title_text,
                   task_line1,
                   task_line2,
                   task_line3,
                   task_line4,
                   value_line1,
                   value_line2,
                   value_line3,
                   source_line1,
                   source_line2,
                   result_line);

    if (UartService_RequestTx(&console->uart, render_buffer) == INFRA_STATUS_OK)
    {
        console->view.layout_drawn = 1U;
        console->view.full_refresh_required = 0U;
        AppConsoleOutput_ClearDirty(console);
    }
}

static void AppConsoleOutput_Render(AppConsole *console)
{
    if (console == NULL)
    {
        return;
    }

#if (APP_CONSOLE_OUTPUT_PROFILE == APP_CONSOLE_PROFILE_PUTTY)
    if ((console->view.layout_drawn == 0U) || (console->view.full_refresh_required != 0U))
    {
        AppConsole_RenderLayout(console);
        if (console->view.layout_drawn == 0U)
        {
            return;
        }

        console->view.full_refresh_required = 0U;
    }

    AppConsole_RenderDirtyLines(console);
#else
    if ((console->view.layout_drawn == 0U) ||
        (console->view.full_refresh_required != 0U) ||
        (AppConsoleOutput_HasGuiDirty(console) != 0U))
    {
        AppConsoleOutput_RenderGuiSnapshot(console);
    }
#endif
}

#endif
