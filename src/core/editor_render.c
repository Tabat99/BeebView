#include "viewbbc/editor.h"
#include "viewbbc/version.h"
#include "viewbbc/ruler.h"

#include <stdio.h>
#include <string.h>

#define VIEWBBC_BASE_TEXT_X 3
#define VIEWBBC_TEXT_Y 1

static size_t visible_text_rows(const ViewBBCEditor *editor) {
    return editor->screen.rows > VIEWBBC_TEXT_Y ? (size_t)(editor->screen.rows - VIEWBBC_TEXT_Y) : 1u;
}

static size_t visible_text_cols(const ViewBBCEditor *editor) {
    int available = editor->screen.cols - viewbbc_editor_text_x(editor);
    return available > 0 ? (size_t)available : 1u;
}

static int position_before(size_t line_a, size_t column_a,
                           size_t line_b, size_t column_b) {
    return line_a < line_b || (line_a == line_b && column_a < column_b);
}

static int mouse_selected_cell(const ViewBBCEditor *editor, size_t line, size_t column) {
    if (!editor || !editor->mouse_selection_active) return 0;
    size_t start_line, start_column, end_line, end_column;
    if (!viewbbc_markers_get(&editor->markers, 1u, &start_line, &start_column) ||
        !viewbbc_markers_get(&editor->markers, 2u, &end_line, &end_column)) return 0;
    if (!position_before(start_line, start_column, end_line, end_column)) return 0;
    return !position_before(line, column, start_line, start_column) &&
           position_before(line, column, end_line, end_column);
}

static void put_string(ViewBBCScreen *screen, int x, int y, const char *text) {
    if (!screen || !text || y < 0 || y >= screen->rows) return;
    for (int i = 0; text[i] != '\0' && x + i < screen->cols; ++i) {
        if (x + i >= 0) viewbbc_screen_put(screen, x + i, y, (uint8_t)text[i], VIEWBBC_ATTR_NONE);
    }
}

static void draw_mode_and_ruler(ViewBBCEditor *editor) {
    int x = 0;
    if (editor->replace_active) {
        viewbbc_screen_put(&editor->screen, x++, 0, 'R', VIEWBBC_ATTR_NONE);
        viewbbc_screen_put(&editor->screen, x++, 0, 'P', VIEWBBC_ATTR_NONE);
    }
    if (editor->marker_input == VIEWBBC_MARKER_INPUT_SET) {
        viewbbc_screen_put(&editor->screen, x++, 0, 'M', VIEWBBC_ATTR_NONE);
        viewbbc_screen_put(&editor->screen, x++, 0, 'K', VIEWBBC_ATTR_NONE);
    }
    if (editor->format_mode) viewbbc_screen_put(&editor->screen, x++, 0, 'F', VIEWBBC_ATTR_NONE);
    if (editor->justify_mode) viewbbc_screen_put(&editor->screen, x++, 0, 'J', VIEWBBC_ATTR_NONE);
    if (editor->insert_mode) viewbbc_screen_put(&editor->screen, x++, 0, 'I', VIEWBBC_ATTR_NONE);

    size_t width = visible_text_cols(editor);
    for (size_t i = 0; i < width; ++i) {
        size_t column = editor->viewport_left + i;
        if (column >= VIEWBBC_RULER_WIDTH) break;
        uint32_t ch = viewbbc_ruler_char_at(column);
        viewbbc_screen_put(&editor->screen, viewbbc_editor_text_x(editor) + (int)i, 0, ch, VIEWBBC_ATTR_NONE);
    }

    if (editor->rowcols && editor->screen.cols > 0) {
        char rc[64];
        (void)snprintf(rc, sizeof(rc), "R:%zu C:%zu",
                       editor->cursor_line + 1u, editor->cursor_column + 1u);
        size_t len = strlen(rc);
        int start = editor->screen.cols - (int)len;
        if (start < viewbbc_editor_text_x(editor)) start = viewbbc_editor_text_x(editor);
        put_string(&editor->screen, start, 0, rc);
    }
}

static void format_bytes_free(const ViewBBCEditor *editor, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    size_t bytes = viewbbc_document_bytes_free(&editor->document);
    if (bytes > 1024u * 1024u && bytes % (1024u * 1024u) == 0) {
        (void)snprintf(out, out_size, "%zu MB", bytes / (1024u * 1024u));
    } else if (bytes >= 1024u) {
        (void)snprintf(out, out_size, "%zu KB", bytes / 1024u);
    } else {
        (void)snprintf(out, out_size, "%zu bytes", bytes);
    }
}

static void render_command_prompt(ViewBBCEditor *editor, int y) {
    if (!editor || y < 0 || y >= editor->screen.rows) return;
    put_string(&editor->screen, 0, y, "=>");

    int available = editor->screen.cols - 2;
    if (available <= 0) {
        viewbbc_screen_set_cursor(&editor->screen, 0, y);
        return;
    }

    size_t shown = (size_t)available;
    size_t start = 0u;
    if (editor->command_cursor >= shown)
        start = editor->command_cursor - shown + 1u;
    if (start > editor->command_length) start = editor->command_length;

    size_t remaining = editor->command_length - start;
    size_t copy = remaining < shown ? remaining : shown;
    for (size_t i = 0; i < copy; ++i) {
        size_t index = start + i;
        uint8_t attr = (editor->command_selection_active &&
                        index >= editor->command_selection_start &&
                        index < editor->command_selection_end)
            ? VIEWBBC_ATTR_REVERSE : VIEWBBC_ATTR_NONE;
        viewbbc_screen_put(&editor->screen, 2 + (int)i, y,
                           (uint8_t)editor->command_buffer[index], attr);
    }

    size_t cursor = editor->command_cursor >= start ? editor->command_cursor - start : 0u;
    if (cursor >= shown) cursor = shown - 1u;
    viewbbc_screen_set_cursor(&editor->screen, 2 + (int)cursor, y);
}

static void render_command_mode(ViewBBCEditor *editor) {
    enum { HEADER_LINES = 8 };
    viewbbc_screen_clear(&editor->screen, ' ', VIEWBBC_ATTR_NONE);
    viewbbc_screen_hide_cursor(&editor->screen);
    if (editor->screen.rows <= 0) return;

    size_t rows = (size_t)editor->screen.rows;
    const int confirmation = editor->confirm_action != VIEWBBC_CONFIRM_NONE;
    size_t prompt_lines = editor->output_paging ? 0u : 1u;
    size_t total = HEADER_LINES + editor->console.count + prompt_lines;
    size_t first = total > rows ? total - rows : 0u;
    char line[VIEWBBC_CONSOLE_LINE_MAX + 1];

    for (size_t virtual_line = first, y = 0; virtual_line < total && y < rows;
         ++virtual_line, ++y) {
        if (virtual_line == 0) {
            put_string(&editor->screen, 0, (int)y, "BeebView");
        } else if (virtual_line == 1 || virtual_line == 5) {
            /* Deliberate blank lines, matching VIEW's command screen layout. */
        } else if (virtual_line == 2) {
            char bytes[48];
            format_bytes_free(editor, bytes, sizeof(bytes));
            (void)snprintf(line, sizeof(line), "Bytes free: %s", bytes);
            put_string(&editor->screen, 0, (int)y, line);
        } else if (virtual_line == 3) {
            if (editor->image_mounted) {
                (void)snprintf(line, sizeof(line), "File System: DFS %s",
                               viewbbc_dfs_type_name(editor->mounted_image.type));
            } else {
                (void)snprintf(line, sizeof(line), "File System: Native");
            }
            put_string(&editor->screen, 0, (int)y, line);
        } else if (virtual_line == 4) {
            if (editor->current_file) {
                (void)snprintf(line, sizeof(line), "Editing: %s%s",
                               editor->current_file,
                               editor->current_source_read_only ? " [read only source]" : "");
                put_string(&editor->screen, 0, (int)y, line);
            } else {
                put_string(&editor->screen, 0, (int)y, "Editing: No File");
            }
        } else if (virtual_line == 6) {
            put_string(&editor->screen, 0, (int)y, "Type ? or HELP for help");
        } else if (virtual_line == 7) {
            /* Deliberate blank before command history/prompt. */
        } else if (virtual_line < HEADER_LINES + editor->console.count) {
            const char *history = viewbbc_console_line(&editor->console,
                                                       virtual_line - HEADER_LINES);
            if (history) put_string(&editor->screen, 0, (int)y, history);
        } else if (!editor->output_paging) {
            if (confirmation) {
                (void)snprintf(line, sizeof(line), "%s ",
                               editor->status_message[0] ? editor->status_message : "Confirm? (Y/N)");
                const char *message = line;
                put_string(&editor->screen, 0, (int)y, message);
                size_t length = strlen(message);
                int cursor_x = length < (size_t)editor->screen.cols
                    ? (int)length
                    : editor->screen.cols - 1;
                viewbbc_screen_set_cursor(&editor->screen, cursor_x, (int)y);
            } else {
                render_command_prompt(editor, (int)y);
            }
        }
    }

    if (editor->command_view_selection_active && editor->screen.cols > 0 && editor->screen.rows > 0) {
        size_t cells = (size_t)editor->screen.cols * (size_t)editor->screen.rows;
        size_t start = editor->command_view_selection_start < cells ? editor->command_view_selection_start : cells;
        size_t end = editor->command_view_selection_end < cells ? editor->command_view_selection_end : cells;
        for (size_t pos = start; pos < end; ++pos) {
            int sx = (int)(pos % (size_t)editor->screen.cols);
            int sy = (int)(pos / (size_t)editor->screen.cols);
            ViewBBCCell cell = viewbbc_screen_get(&editor->screen, sx, sy);
            viewbbc_screen_put(&editor->screen, sx, sy, cell.ch, (uint8_t)(cell.attr | VIEWBBC_ATTR_REVERSE));
        }
    }
}

static void render_text_mode(ViewBBCEditor *editor) {
    viewbbc_screen_clear(&editor->screen, ' ', VIEWBBC_ATTR_NONE);
    draw_mode_and_ruler(editor);

    size_t rows = visible_text_rows(editor);
    size_t cols = visible_text_cols(editor);
    size_t line_count = viewbbc_document_line_count(&editor->document);

    for (size_t sy = 0; sy < rows; ++sy) {
        size_t line_index = editor->viewport_top + sy;
        int screen_y = VIEWBBC_TEXT_Y + (int)sy;

        if (line_index < line_count) {
            int text_x = viewbbc_editor_text_x(editor);
            if (editor->line_numbers) {
                int number_width = text_x - VIEWBBC_BASE_TEXT_X - 1;
                char number[32];
                (void)snprintf(number, sizeof(number), "%*zu", number_width, line_index + 1u);
                put_string(&editor->screen, VIEWBBC_BASE_TEXT_X, screen_y, number);
            }
            const char *stored_command = viewbbc_document_command_at(&editor->document, line_index);
            if (line_index == editor->cursor_line && editor->edit_command_active)
                stored_command = editor->edit_command_buffer;
            if (stored_command[0]) {
                viewbbc_screen_put(&editor->screen, 0, screen_y, (uint8_t)stored_command[0], VIEWBBC_ATTR_NONE);
                if (stored_command[1]) viewbbc_screen_put(&editor->screen, 1, screen_y, (uint8_t)stored_command[1], VIEWBBC_ATTR_NONE);
            }
            size_t length = viewbbc_document_line_length(&editor->document, line_index);
            for (size_t sx = 0; sx < cols; ++sx) {
                size_t column = editor->viewport_left + sx;
                if (column >= length) break;
                uint8_t attr = mouse_selected_cell(editor, line_index, column)
                    ? VIEWBBC_ATTR_REVERSE : VIEWBBC_ATTR_NONE;
                viewbbc_screen_put(&editor->screen,
                                   viewbbc_editor_text_x(editor) + (int)sx,
                                   screen_y,
                                   viewbbc_document_char_at(&editor->document, line_index, column),
                                   attr);
            }
        } else if (line_index == line_count) {
            for (size_t sx = 0; sx < cols; ++sx) {
                viewbbc_screen_put(&editor->screen, viewbbc_editor_text_x(editor) + (int)sx, screen_y, '*', VIEWBBC_ATTR_NONE);
            }
            break;
        } else {
            break;
        }
    }

    int cursor_y = VIEWBBC_TEXT_Y + (int)(editor->cursor_line - editor->viewport_top);
    int cursor_x = editor->edit_command_active ? (int)editor->edit_command_length
        : viewbbc_editor_text_x(editor) + (int)(editor->cursor_column - editor->viewport_left);
    viewbbc_screen_set_cursor(&editor->screen, cursor_x, cursor_y);
}

void viewbbc_editor_render(ViewBBCEditor *editor) {
    if (!editor) return;
    if (editor->mode == VIEWBBC_MODE_COMMAND) render_command_mode(editor);
    else render_text_mode(editor);
}
