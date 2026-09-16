#include "viewbbc/console.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void viewbbc_console_clear(ViewBBCConsole *console) {
    if (!console) return;
    *console = (ViewBBCConsole){0};
}

int viewbbc_console_add(ViewBBCConsole *console, const char *text) {
    if (!console || !text) return 0;

    size_t slot;
    if (console->count < VIEWBBC_CONSOLE_MAX_LINES) {
        slot = (console->first + console->count) % VIEWBBC_CONSOLE_MAX_LINES;
        console->count++;
    } else {
        slot = console->first;
        console->first = (console->first + 1u) % VIEWBBC_CONSOLE_MAX_LINES;
    }

    size_t length = 0;
    while (length < VIEWBBC_CONSOLE_LINE_MAX && text[length] != '\0') length++;
    memcpy(console->lines[slot], text, length);
    console->lines[slot][length] = '\0';
    return 1;
}

int viewbbc_console_addf(ViewBBCConsole *console, const char *format, ...) {
    if (!console || !format) return 0;

    char buffer[VIEWBBC_CONSOLE_LINE_MAX + 1];
    va_list ap;
    va_start(ap, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);
    if (written < 0) return 0;
    return viewbbc_console_add(console, buffer);
}

const char *viewbbc_console_line(const ViewBBCConsole *console, size_t index) {
    if (!console || index >= console->count) return NULL;
    size_t slot = (console->first + index) % VIEWBBC_CONSOLE_MAX_LINES;
    return console->lines[slot];
}

int viewbbc_console_remove_last(ViewBBCConsole *console) {
    if (!console || console->count == 0) return 0;
    console->count--;
    size_t slot = (console->first + console->count) % VIEWBBC_CONSOLE_MAX_LINES;
    console->lines[slot][0] = '\0';
    if (console->count == 0) console->first = 0;
    return 1;
}
