#include "viewbbc/ruler.h"

uint32_t viewbbc_ruler_char_at(size_t column) {
    if (column == VIEWBBC_RULER_WIDTH - 1u) return '<';
    if ((column + 1u) % 8u == 0u) return '*';
    return '.';
}

int viewbbc_ruler_next_tab_stop(size_t column, size_t *column_out) {
    if (!column_out || column >= VIEWBBC_RULER_WIDTH - 1u) return 0;

    for (size_t next = column + 1u; next < VIEWBBC_RULER_WIDTH; ++next) {
        if (viewbbc_ruler_char_at(next) == '*') {
            *column_out = next;
            return 1;
        }
    }
    return 0;
}
