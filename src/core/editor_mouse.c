#include "viewbbc/editor_mouse.h"
#include "viewbbc/editor.h"

#include <ctype.h>
#include <stdint.h>

#define VIEWBBC_TEXT_Y 1

static int position_compare(size_t line_a, size_t column_a,
                            size_t line_b, size_t column_b) {
    if (line_a < line_b) return -1;
    if (line_a > line_b) return 1;
    if (column_a < column_b) return -1;
    if (column_a > column_b) return 1;
    return 0;
}

static int screen_to_document(ViewBBCEditor *editor, int screen_x, int screen_y,
                              size_t *line_out, size_t *column_out) {
    if (!editor || !line_out || !column_out || editor->mode != VIEWBBC_MODE_TEXT)
        return 0;
    if (screen_x < 0 || screen_y < VIEWBBC_TEXT_Y)
        return 0;

    size_t line_count = viewbbc_document_line_count(&editor->document);
    if (line_count == 0u) return 0;

    size_t line = editor->viewport_top + (size_t)(screen_y - VIEWBBC_TEXT_Y);
    if (line >= line_count) line = line_count - 1u;

    int text_x = viewbbc_editor_text_x(editor);
    size_t column = screen_x < text_x ? 0u
        : editor->viewport_left + (size_t)(screen_x - text_x);
    size_t length = viewbbc_document_line_length(&editor->document, line);
    if (column > length) column = length;

    *line_out = line;
    *column_out = column;
    return 1;
}

static void move_cursor(ViewBBCEditor *editor, size_t line, size_t column) {
    editor->cursor_line = line;
    editor->cursor_column = column;
    editor->status_message[0] = '\0';
    viewbbc_editor_render(editor);
}

static void position_after_cell(const ViewBBCEditor *editor,
                                size_t line, size_t column,
                                size_t *line_out, size_t *column_out) {
    size_t length = viewbbc_document_line_length(&editor->document, line);
    *line_out = line;
    *column_out = column < length ? column + 1u : column;
}

static void set_selection(ViewBBCEditor *editor,
                          size_t anchor_line, size_t anchor_column,
                          size_t line, size_t column,
                          int include_cell) {
    size_t start_line, start_column, end_line, end_column;
    if (position_compare(line, column, anchor_line, anchor_column) < 0) {
        start_line = line;
        start_column = column;
        if (include_cell)
            position_after_cell(editor, anchor_line, anchor_column, &end_line, &end_column);
        else {
            end_line = anchor_line;
            end_column = anchor_column;
        }
    } else {
        start_line = anchor_line;
        start_column = anchor_column;
        if (include_cell)
            position_after_cell(editor, line, column, &end_line, &end_column);
        else {
            end_line = line;
            end_column = column;
        }
    }
    (void)viewbbc_markers_set(&editor->markers, 1u, start_line, start_column);
    (void)viewbbc_markers_set(&editor->markers, 2u, end_line, end_column);
    editor->mouse_selection_active =
        position_compare(start_line, start_column, end_line, end_column) != 0;
}

void viewbbc_editor_mouse_click(ViewBBCEditor *editor, int screen_x, int screen_y) {
    size_t line, column;
    if (!screen_to_document(editor, screen_x, screen_y, &line, &column)) return;
    editor->mouse_selection_active = 0;
    editor->mouse_dragging = 0;
    move_cursor(editor, line, column);
}

void viewbbc_editor_mouse_shift_click(ViewBBCEditor *editor, int screen_x, int screen_y) {
    size_t line, column;
    if (!screen_to_document(editor, screen_x, screen_y, &line, &column)) return;

    size_t anchor_line = editor->cursor_line;
    size_t anchor_column = editor->cursor_column;
    if (editor->mouse_selection_active) {
        (void)viewbbc_markers_get(&editor->markers, 1u, &anchor_line, &anchor_column);
    }
    editor->mouse_anchor_line = anchor_line;
    editor->mouse_anchor_column = anchor_column;
    set_selection(editor, anchor_line, anchor_column, line, column, 1);
    editor->cursor_line = line;
    editor->cursor_column = column;
    editor->status_message[0] = '\0';
    viewbbc_editor_render(editor);
}

static int word_class(unsigned char ch) {
    if (isalnum((int)ch) || ch == '_') return 2;
    if (ch == ' ' || ch == '\t') return 1;
    return 0;
}

void viewbbc_editor_mouse_select_word(ViewBBCEditor *editor, int screen_x, int screen_y) {
    size_t line, column;
    if (!screen_to_document(editor, screen_x, screen_y, &line, &column)) return;
    size_t length = viewbbc_document_line_length(&editor->document, line);
    if (length == 0u || column >= length) {
        viewbbc_editor_mouse_click(editor, screen_x, screen_y);
        return;
    }

    int cls = word_class(viewbbc_document_char_at(&editor->document, line, column));
    size_t start = column;
    size_t end = column + 1u;
    while (start > 0u && word_class(viewbbc_document_char_at(&editor->document, line, start - 1u)) == cls)
        --start;
    while (end < length && word_class(viewbbc_document_char_at(&editor->document, line, end)) == cls)
        ++end;

    (void)viewbbc_markers_set(&editor->markers, 1u, line, start);
    (void)viewbbc_markers_set(&editor->markers, 2u, line, end);
    editor->mouse_selection_active = start < end;
    editor->mouse_anchor_line = line;
    editor->mouse_anchor_column = start;
    editor->cursor_line = line;
    editor->cursor_column = end;
    editor->status_message[0] = '\0';
    viewbbc_editor_render(editor);
}

void viewbbc_editor_mouse_select_line(ViewBBCEditor *editor, int screen_x, int screen_y) {
    size_t line, column;
    (void)column;
    if (!screen_to_document(editor, screen_x, screen_y, &line, &column)) return;
    size_t line_count = viewbbc_document_line_count(&editor->document);
    size_t length = viewbbc_document_line_length(&editor->document, line);
    size_t end_line = line;
    size_t end_column = length;
    if (line + 1u < line_count) {
        end_line = line + 1u;
        end_column = 0u;
    }
    (void)viewbbc_markers_set(&editor->markers, 1u, line, 0u);
    (void)viewbbc_markers_set(&editor->markers, 2u, end_line, end_column);
    editor->mouse_selection_active = line < end_line || length != 0u;
    editor->mouse_anchor_line = line;
    editor->mouse_anchor_column = 0u;
    editor->cursor_line = line;
    editor->cursor_column = length;
    editor->status_message[0] = '\0';
    viewbbc_editor_render(editor);
}

void viewbbc_editor_select_all(ViewBBCEditor *editor) {
    if (!editor || editor->mode != VIEWBBC_MODE_TEXT) return;
    size_t line_count = viewbbc_document_line_count(&editor->document);
    if (line_count == 0u) return;
    size_t last = line_count - 1u;
    size_t last_length = viewbbc_document_line_length(&editor->document, last);
    (void)viewbbc_markers_set(&editor->markers, 1u, 0u, 0u);
    (void)viewbbc_markers_set(&editor->markers, 2u, last, last_length);
    editor->mouse_selection_active = (last != 0u || last_length != 0u);
    editor->mouse_anchor_line = 0u;
    editor->mouse_anchor_column = 0u;
    editor->cursor_line = last;
    editor->cursor_column = last_length;
    editor->status_message[0] = '\0';
    viewbbc_editor_render(editor);
}

void viewbbc_editor_mouse_drag_begin(ViewBBCEditor *editor, int screen_x, int screen_y) {
    size_t line, column;
    if (!screen_to_document(editor, screen_x, screen_y, &line, &column)) return;
    editor->mouse_dragging = 1;
    editor->mouse_drag_moved = 0;
    editor->mouse_selection_active = 0;
    editor->mouse_anchor_line = line;
    editor->mouse_anchor_column = column;
    move_cursor(editor, line, column);
}

void viewbbc_editor_mouse_drag_update(ViewBBCEditor *editor, int screen_x, int screen_y) {
    size_t line, column;
    if (!editor || !editor->mouse_dragging) return;
    if (!screen_to_document(editor, screen_x, screen_y, &line, &column)) return;
    if (line == editor->mouse_anchor_line && column == editor->mouse_anchor_column &&
        !editor->mouse_drag_moved) {
        move_cursor(editor, line, column);
        return;
    }
    editor->mouse_drag_moved = 1;
    set_selection(editor, editor->mouse_anchor_line, editor->mouse_anchor_column,
                  line, column, 1);
    editor->cursor_line = line;
    editor->cursor_column = column;
    editor->status_message[0] = '\0';
    viewbbc_editor_render(editor);
}

void viewbbc_editor_mouse_drag_autoscroll(ViewBBCEditor *editor, int screen_x, int direction) {
    if (!editor || !editor->mouse_dragging || editor->mode != VIEWBBC_MODE_TEXT || direction == 0)
        return;
    size_t line_count = viewbbc_document_line_count(&editor->document);
    if (line_count == 0u) return;

    size_t visible_rows = editor->screen.rows > VIEWBBC_TEXT_Y
        ? (size_t)(editor->screen.rows - VIEWBBC_TEXT_Y) : 1u;
    size_t max_top = line_count > visible_rows ? line_count - visible_rows : 0u;
    if (direction < 0) {
        if (editor->viewport_top > 0u) --editor->viewport_top;
    } else if (editor->viewport_top < max_top) {
        ++editor->viewport_top;
    }

    int y = direction < 0 ? VIEWBBC_TEXT_Y : editor->screen.rows - 1;
    if (screen_x < viewbbc_editor_text_x(editor)) screen_x = viewbbc_editor_text_x(editor);
    if (screen_x >= editor->screen.cols) screen_x = editor->screen.cols - 1;
    viewbbc_editor_mouse_drag_update(editor, screen_x, y);
}

void viewbbc_editor_mouse_drag_end(ViewBBCEditor *editor, int screen_x, int screen_y) {
    if (!editor || !editor->mouse_dragging) return;
    if (!editor->mouse_drag_moved) {
        editor->mouse_dragging = 0;
        viewbbc_editor_mouse_click(editor, screen_x, screen_y);
        return;
    }
    viewbbc_editor_mouse_drag_update(editor, screen_x, screen_y);
    editor->mouse_dragging = 0;
    viewbbc_editor_render(editor);
}

void viewbbc_editor_mouse_wheel(ViewBBCEditor *editor, int steps) {
    if (!editor || editor->mode != VIEWBBC_MODE_TEXT || steps == 0) return;

    size_t line_count = viewbbc_document_line_count(&editor->document);
    if (line_count == 0u) return;
    size_t last_line = line_count - 1u;
    uint64_t magnitude = steps > 0 ? (uint64_t)steps : (uint64_t)(-(int64_t)steps);
    size_t amount = magnitude > (uint64_t)SIZE_MAX ? SIZE_MAX : (size_t)magnitude;

    if (steps > 0) {
        size_t move = amount < editor->cursor_line ? amount : editor->cursor_line;
        editor->cursor_line -= move;
        editor->viewport_top = move < editor->viewport_top ? editor->viewport_top - move : 0u;
    } else {
        size_t remaining = last_line - editor->cursor_line;
        size_t move = amount < remaining ? amount : remaining;
        editor->cursor_line += move;
        if (editor->viewport_top <= (size_t)-1 - move)
            editor->viewport_top += move;
    }

    size_t visible_rows = editor->screen.rows > VIEWBBC_TEXT_Y
        ? (size_t)(editor->screen.rows - VIEWBBC_TEXT_Y) : 1u;
    size_t max_top = line_count > visible_rows ? line_count - visible_rows : 0u;
    if (editor->viewport_top > max_top) editor->viewport_top = max_top;

    size_t length = viewbbc_document_line_length(&editor->document, editor->cursor_line);
    if (editor->cursor_column > length) editor->cursor_column = length;
    viewbbc_editor_render(editor);
}
