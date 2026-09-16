#include "viewbbc/output.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void viewbbc_output_clear(ViewBBCCommandOutput *output) {
    if (!output) return;
    *output = (ViewBBCCommandOutput){0};
}

int viewbbc_output_add(ViewBBCCommandOutput *output, const char *text) {
    if (!output || !text) return 0;
    if (output->line_count >= VIEWBBC_OUTPUT_MAX_LINES) {
        output->truncated = 1;
        return 0;
    }

    size_t length = strlen(text);
    if (length > VIEWBBC_OUTPUT_LINE_MAX) {
        length = VIEWBBC_OUTPUT_LINE_MAX;
        output->truncated = 1;
    }
    memcpy(output->lines[output->line_count], text, length);
    output->lines[output->line_count][length] = '\0';
    output->line_count++;
    return 1;
}

int viewbbc_output_addf(ViewBBCCommandOutput *output, const char *format, ...) {
    if (!output || !format) return 0;
    if (output->line_count >= VIEWBBC_OUTPUT_MAX_LINES) {
        output->truncated = 1;
        return 0;
    }

    char buffer[VIEWBBC_OUTPUT_LINE_MAX + 1];
    va_list ap;
    va_start(ap, format);
    int written = vsnprintf(buffer, sizeof(buffer), format, ap);
    va_end(ap);
    if (written < 0) return 0;
    if ((size_t)written >= sizeof(buffer)) output->truncated = 1;
    return viewbbc_output_add(output, buffer);
}
