#include "viewbbc/screen.h"

#include <stdint.h>
#include <stdlib.h>

static int checked_cell_count(int cols, int rows, size_t *count_out) {
    if (!count_out || cols <= 0 || rows <= 0) return 0;
    size_t c = (size_t)cols;
    size_t r = (size_t)rows;
    if (c > SIZE_MAX / r) return 0;
    size_t count = c * r;
    if (count > SIZE_MAX / sizeof(ViewBBCCell)) return 0;
    *count_out = count;
    return 1;
}

static size_t index_of(const ViewBBCScreen *screen, int x, int y) {
    return (size_t)y * (size_t)screen->cols + (size_t)x;
}

int viewbbc_screen_init(ViewBBCScreen *screen, int cols, int rows) {
    if (!screen) return 0;
    *screen = (ViewBBCScreen){0};

    size_t cell_count;
    if (!checked_cell_count(cols, rows, &cell_count)) return 0;

    screen->cells = calloc(cell_count, sizeof(ViewBBCCell));
    if (!screen->cells) return 0;

    screen->cols = cols;
    screen->rows = rows;
    screen->cursor_visible = 1;
    viewbbc_screen_clear(screen, ' ', VIEWBBC_ATTR_NONE);
    return 1;
}

int viewbbc_screen_resize(ViewBBCScreen *screen, int cols, int rows) {
    if (!screen || !screen->cells) return 0;
    if (cols == screen->cols && rows == screen->rows) return 1;

    size_t cell_count;
    if (!checked_cell_count(cols, rows, &cell_count)) return 0;
    ViewBBCCell *cells = calloc(cell_count, sizeof(ViewBBCCell));
    if (!cells) return 0;

    free(screen->cells);
    screen->cells = cells;
    screen->cols = cols;
    screen->rows = rows;
    screen->cursor_x = 0;
    screen->cursor_y = 0;
    screen->cursor_visible = 1;
    viewbbc_screen_clear(screen, ' ', VIEWBBC_ATTR_NONE);
    return 1;
}

void viewbbc_screen_destroy(ViewBBCScreen *screen) {
    if (!screen) return;
    free(screen->cells);
    *screen = (ViewBBCScreen){0};
}

void viewbbc_screen_clear(ViewBBCScreen *screen, uint32_t ch, uint8_t attr) {
    if (!screen || !screen->cells || screen->cols <= 0 || screen->rows <= 0) return;
    for (int y = 0; y < screen->rows; ++y) {
        for (int x = 0; x < screen->cols; ++x) {
            ViewBBCCell *cell = &screen->cells[index_of(screen, x, y)];
            cell->ch = ch;
            cell->attr = attr;
        }
    }
}

void viewbbc_screen_put(ViewBBCScreen *screen, int x, int y, uint32_t ch, uint8_t attr) {
    if (!screen || !screen->cells || x < 0 || y < 0 || x >= screen->cols || y >= screen->rows) return;
    ViewBBCCell *cell = &screen->cells[index_of(screen, x, y)];
    cell->ch = ch;
    cell->attr = attr;
}

ViewBBCCell viewbbc_screen_get(const ViewBBCScreen *screen, int x, int y) {
    ViewBBCCell blank = { ' ', VIEWBBC_ATTR_NONE };
    if (!screen || !screen->cells || x < 0 || y < 0 || x >= screen->cols || y >= screen->rows) return blank;
    return screen->cells[index_of(screen, x, y)];
}

void viewbbc_screen_set_cursor(ViewBBCScreen *screen, int x, int y) {
    if (!screen || screen->cols <= 0 || screen->rows <= 0) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= screen->cols) x = screen->cols - 1;
    if (y >= screen->rows) y = screen->rows - 1;
    screen->cursor_x = x;
    screen->cursor_y = y;
    screen->cursor_visible = 1;
}

void viewbbc_screen_hide_cursor(ViewBBCScreen *screen) {
    if (!screen) return;
    screen->cursor_visible = 0;
}
