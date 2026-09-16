#include "viewbbc/editor.h"
#include "viewbbc/editor_commands.h"
#include "viewbbc/blocks.h"
#include "viewbbc/formatter.h"
#include "viewbbc/ruler.h"
#include "viewbbc/view_commands.h"
#include "editor_command_line.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VIEWBBC_BASE_TEXT_X 3
#define VIEWBBC_TEXT_Y 1

static size_t decimal_digits(size_t value) {
    size_t digits = 1u;
    while (value >= 10u) { value /= 10u; ++digits; }
    return digits;
}

int viewbbc_editor_text_x(const ViewBBCEditor *editor) {
    if (!editor || !editor->line_numbers) return VIEWBBC_BASE_TEXT_X;
    size_t lines = viewbbc_document_line_count(&editor->document);
    size_t digits = decimal_digits(lines ? lines : 1u);
    if (digits > (size_t)INT_MAX - VIEWBBC_BASE_TEXT_X - 1u) return VIEWBBC_BASE_TEXT_X;
    return VIEWBBC_BASE_TEXT_X + (int)digits + 1;
}

static size_t current_line_length(const ViewBBCEditor *editor) {
    return viewbbc_document_line_length(&editor->document, editor->cursor_line);
}

static size_t visible_text_rows(const ViewBBCEditor *editor) {
    return editor->screen.rows > VIEWBBC_TEXT_Y ? (size_t)(editor->screen.rows - VIEWBBC_TEXT_Y) : 1u;
}

static size_t visible_text_cols(const ViewBBCEditor *editor) {
    int available = editor->screen.cols - viewbbc_editor_text_x(editor);
    return available > 0 ? (size_t)available : 1u;
}

static void clamp_column(ViewBBCEditor *editor) {
    size_t length = current_line_length(editor);
    if (editor->cursor_column > length) editor->cursor_column = length;
}

static void set_block_status(ViewBBCEditor *editor, ViewBBCBlockResult result) {
    if (!editor) return;
    switch (result) {
        case VIEWBBC_BLOCK_OK:
            editor->status_message[0] = '\0';
            break;
        case VIEWBBC_BLOCK_MARKERS_UNSET:
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Markers 1 and 2 not set");
            break;
        case VIEWBBC_BLOCK_MARKERS_INVALID:
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Markers 1 and 2 invalid");
            break;
        case VIEWBBC_BLOCK_DESTINATION_INSIDE:
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Destination inside block");
            break;
        case VIEWBBC_BLOCK_NO_MEMORY:
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "No room");
            break;
    }
}

static void keep_cursor_visible(ViewBBCEditor *editor) {
    size_t rows = visible_text_rows(editor);
    size_t cols = visible_text_cols(editor);

    if (editor->cursor_line < editor->viewport_top) {
        editor->viewport_top = editor->cursor_line;
    } else if (editor->cursor_line >= editor->viewport_top + rows) {
        editor->viewport_top = editor->cursor_line - rows + 1;
    }

    if (editor->cursor_column < editor->viewport_left) {
        editor->viewport_left = editor->cursor_column;
    } else if (editor->cursor_column >= editor->viewport_left + cols) {
        editor->viewport_left = editor->cursor_column - cols + 1;
    }
}

static void move_page(ViewBBCEditor *editor, int direction) {
    if (!editor || direction == 0) return;

    const size_t line_count = viewbbc_document_line_count(&editor->document);
    if (line_count == 0) return;

    const size_t page_rows = visible_text_rows(editor);
    const size_t last_line = line_count - 1u;
    const size_t max_viewport_top = line_count > page_rows ? line_count - page_rows : 0u;

    if (direction < 0) {
        editor->cursor_line = editor->cursor_line > page_rows
            ? editor->cursor_line - page_rows
            : 0u;
        editor->viewport_top = editor->viewport_top > page_rows
            ? editor->viewport_top - page_rows
            : 0u;
    } else {
        if (editor->cursor_line >= last_line ||
            page_rows > last_line - editor->cursor_line) {
            editor->cursor_line = last_line;
        } else {
            editor->cursor_line += page_rows;
        }
        if (editor->viewport_top >= max_viewport_top ||
            page_rows > max_viewport_top - editor->viewport_top) {
            editor->viewport_top = max_viewport_top;
        } else {
            editor->viewport_top += page_rows;
        }
    }

    clamp_column(editor);
}


void viewbbc_editor_set_line_numbers(ViewBBCEditor *editor, int enabled) {
    if (!editor) return;
    editor->line_numbers = enabled ? 1 : 0;
    keep_cursor_visible(editor);
}

void viewbbc_editor_set_rowcols(ViewBBCEditor *editor, int enabled) {
    if (!editor) return;
    editor->rowcols = enabled ? 1 : 0;
}

int viewbbc_editor_resize(ViewBBCEditor *editor, int cols, int rows) {
    if (!editor || cols <= VIEWBBC_BASE_TEXT_X || rows <= VIEWBBC_TEXT_Y) return 0;
    if (!viewbbc_screen_resize(&editor->screen, cols, rows)) return 0;
    keep_cursor_visible(editor);
    viewbbc_editor_render(editor);
    return 1;
}
int viewbbc_editor_init(ViewBBCEditor *editor, int cols, int rows) {
    if (!editor) return 0;
    *editor = (ViewBBCEditor){0};
    if (!viewbbc_screen_init(&editor->screen, cols, rows)) return 0;
    if (!viewbbc_document_init(&editor->document)) {
        viewbbc_screen_destroy(&editor->screen);
        return 0;
    }
    if (!viewbbc_fs_init(&editor->filesystem)) {
        viewbbc_document_destroy(&editor->document);
        viewbbc_screen_destroy(&editor->screen);
        return 0;
    }
    viewbbc_output_clear(&editor->command_output);
    viewbbc_console_clear(&editor->console);
    editor->running = 1;
    editor->format_mode = 1;
    editor->justify_mode = 1;
    editor->insert_mode = 1;
    editor->fold_mode = 1;
    editor->screen_mode = 3;
    editor->mode = VIEWBBC_MODE_COMMAND;
    viewbbc_markers_clear(&editor->markers);
    viewbbc_editor_render(editor);
    return 1;
}

void viewbbc_editor_destroy(ViewBBCEditor *editor) {
    if (!editor) return;
    if (editor->image_mounted) viewbbc_dfs_close(&editor->mounted_image);
    free(editor->mounted_image_path);
    free(editor->current_file);
    viewbbc_fs_destroy(&editor->filesystem);
    viewbbc_document_destroy(&editor->document);
    viewbbc_screen_destroy(&editor->screen);
    editor->running = 0;
}

static void adjust_markers_after_same_line_delete(ViewBBCEditor *editor,
                                                  size_t line, size_t column,
                                                  size_t removed) {
    if (!editor || removed == 0) return;
    size_t deleted_end = column + removed;
    for (unsigned n = 1u; n <= VIEWBBC_MARKER_COUNT; ++n) {
        size_t ml, mc;
        if (!viewbbc_markers_get(&editor->markers, n, &ml, &mc) || ml != line) continue;
        if (mc >= column && mc <= deleted_end) (void)viewbbc_markers_unset(&editor->markers, n);
        else if (mc > deleted_end) (void)viewbbc_markers_set(&editor->markers, n, ml, mc - removed);
    }
}
void viewbbc_editor_insert_char(ViewBBCEditor *editor, uint32_t ch) {
    if (!editor || ch < 32 || ch > 255) return;

    if (editor->confirm_action != VIEWBBC_CONFIRM_NONE) {
        if (ch == 'y' || ch == 'Y') viewbbc_editor_handle_confirmation(editor, 1);
        else if (ch == 'n' || ch == 'N') viewbbc_editor_handle_confirmation(editor, 0);
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->mode == VIEWBBC_MODE_TEXT && editor->edit_command_active) {
        if (isalpha((unsigned char)ch) && editor->edit_command_length < 2u) {
            editor->edit_command_buffer[editor->edit_command_length++] = (char)toupper((unsigned char)ch);
            editor->edit_command_buffer[editor->edit_command_length] = '\0';
        } else if (editor->edit_command_length >= 2u) {
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Press RETURN to set command");
        } else {
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Command code must be two letters");
        }
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->mode == VIEWBBC_MODE_TEXT && editor->replace_active) {
        if (ch == 'y' || ch == 'Y') (void)viewbbc_view_replace_response(editor, 1);
        else if (ch == 'n' || ch == 'N') (void)viewbbc_view_replace_response(editor, 0);
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->mode == VIEWBBC_MODE_TEXT && editor->marker_input != VIEWBBC_MARKER_INPUT_NONE) {
        ViewBBCMarkerInput action = editor->marker_input;
        editor->marker_input = VIEWBBC_MARKER_INPUT_NONE;
        if (ch >= '1' && ch <= '6') {
            unsigned number = (unsigned)(ch - '0');
            if (action == VIEWBBC_MARKER_INPUT_SET) {
                (void)viewbbc_markers_set(&editor->markers, number, editor->cursor_line, editor->cursor_column);
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "Marker %u set", number);
            } else {
                size_t line = 0, column = 0;
                if (viewbbc_markers_get(&editor->markers, number, &line, &column) &&
                    line < viewbbc_document_line_count(&editor->document)) {
                    editor->cursor_line = line;
                    size_t length = viewbbc_document_line_length(&editor->document, line);
                    editor->cursor_column = column > length ? length : column;
                    keep_cursor_visible(editor);
                } else {
                    (void)snprintf(editor->status_message, sizeof(editor->status_message), "Marker %u not set", number);
                }
            }
        } else {
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Marker number must be 1 to 6");
        }
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->mode == VIEWBBC_MODE_TEXT && editor->delete_to_char_pending) {
        editor->delete_to_char_pending = 0;
        size_t removed = 0;
        if (viewbbc_document_delete_to_char(&editor->document, editor->cursor_line,
                                             editor->cursor_column, (uint8_t)ch, &removed)) {
            adjust_markers_after_same_line_delete(editor, editor->cursor_line,
                                                  editor->cursor_column, removed);
            editor->file_modified = 1;
            editor->status_message[0] = '\0';
        } else {
            (void)snprintf(editor->status_message, sizeof(editor->status_message),
                           "Character not found on line");
        }
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->mode == VIEWBBC_MODE_COMMAND) {
        if (editor->output_paging) {
            if (ch == ' ') viewbbc_editor_continue_output(editor, 0);
            viewbbc_editor_render(editor);
            return;
        }
        viewbbc_editor_command_reset_navigation(editor);
        (void)viewbbc_editor_command_delete_selection(editor);
        if (editor->command_length < VIEWBBC_COMMAND_MAX) {
            char byte = (char)ch;
            if (!viewbbc_editor_command_insert_bytes(editor, editor->command_cursor, &byte, 1u)) {
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "Command too long");
            }
        } else {
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Command too long");
        }
        viewbbc_editor_render(editor);
        return;
    }

    int ok = editor->insert_mode
        ? viewbbc_document_insert_char(&editor->document, editor->cursor_line, editor->cursor_column, (uint8_t)ch)
        : viewbbc_document_overwrite_char(&editor->document, editor->cursor_line, editor->cursor_column, (uint8_t)ch);

    if (ok) {
        editor->file_modified = 1;
        editor->cursor_column++;
        keep_cursor_visible(editor);
    } else {
        (void)snprintf(editor->status_message, sizeof(editor->status_message), "No room");
    }
    viewbbc_editor_render(editor);
}

void viewbbc_editor_handle_key(ViewBBCEditor *editor, ViewBBCKey key) {
    if (!editor) return;

    /* F12 is the BBC Micro BREAK key: a non-destructive emergency return to
       a clean command prompt.  It is deliberately handled before every
       transient mode/confirmation. */
    if (key == VIEWBBC_KEY_BREAK) {
        if (editor->replace_active) viewbbc_view_cancel_replace(editor);
        editor->confirm_action = VIEWBBC_CONFIRM_NONE;
        editor->output_paging = 0;
        editor->output_paging_continuous = 0;
        editor->output_page_next = 0;
        editor->help_visible = 0;
        editor->edit_command_active = 0;
        editor->marker_input = VIEWBBC_MARKER_INPUT_NONE;
        editor->mouse_dragging = 0;
        editor->mouse_drag_moved = 0;
        editor->mouse_selection_active = 0;
        editor->print_requested = 0;
        editor->config_mode_requested = 0;
        editor->command_buffer[0] = '\0';
        editor->command_length = 0;
        editor->command_cursor = 0;
        editor->command_mouse_dragging = 0;
        viewbbc_editor_command_clear_selection(editor);
        editor->command_history_browsing = 0;
        editor->status_message[0] = '\0';
        viewbbc_console_clear(&editor->console);
        viewbbc_output_clear(&editor->command_output);
        editor->mode = VIEWBBC_MODE_COMMAND;
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->confirm_action != VIEWBBC_CONFIRM_NONE) {
        if (key == VIEWBBC_KEY_ESCAPE) viewbbc_editor_handle_confirmation(editor, 0);
        viewbbc_editor_render(editor);
        return;
    }

    if (key == VIEWBBC_KEY_CLS) {
        viewbbc_console_clear(&editor->console);
        viewbbc_output_clear(&editor->command_output);
        editor->help_visible = 0;
        editor->status_message[0] = '\0';
        viewbbc_editor_render(editor);
        return;
    }

    if (key == VIEWBBC_KEY_QUIT) {
        viewbbc_editor_request_exit(editor);
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->mode == VIEWBBC_MODE_COMMAND && editor->output_paging) {
        if (key == VIEWBBC_KEY_ESCAPE) viewbbc_editor_continue_output(editor, 1);
        viewbbc_editor_render(editor);
        return;
    }

    if (key == VIEWBBC_KEY_ESCAPE && editor->replace_active) {
        viewbbc_view_cancel_replace(editor);
        viewbbc_editor_render(editor);
        return;
    }

    if (key == VIEWBBC_KEY_ESCAPE) {
        editor->mode = (editor->mode == VIEWBBC_MODE_COMMAND) ? VIEWBBC_MODE_TEXT : VIEWBBC_MODE_COMMAND;
        viewbbc_editor_render(editor);
        return;
    }

    if (key == VIEWBBC_KEY_FIND || key == VIEWBBC_KEY_FIND_REPLACE) {
        const char *prefix = key == VIEWBBC_KEY_FIND ? "SEARCH " : "CHANGE ";
        size_t n = strlen(prefix);
        memcpy(editor->command_buffer, prefix, n + 1u);
        editor->command_length = n;
        editor->command_cursor = n;
        viewbbc_editor_command_clear_selection(editor);
        editor->command_history_browsing = 0;
        editor->command_completion_pending = 0;
        editor->mode = VIEWBBC_MODE_COMMAND;
        (void)snprintf(editor->status_message, sizeof(editor->status_message),
                       "%s", key == VIEWBBC_KEY_FIND ? "Find" : "Find and replace");
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->mode == VIEWBBC_MODE_COMMAND) {
        switch (key) {
            case VIEWBBC_KEY_SHIFT_LEFT:
                viewbbc_editor_command_select_left(editor);
                break;
            case VIEWBBC_KEY_SHIFT_RIGHT:
                viewbbc_editor_command_select_right(editor);
                break;
            case VIEWBBC_KEY_LEFT:
                editor->command_completion_pending = 0;
                if (viewbbc_editor_command_has_selection(editor))
                    editor->command_cursor = editor->command_selection_start;
                else if (editor->command_cursor > 0) editor->command_cursor--;
                viewbbc_editor_command_clear_selection(editor);
                break;
            case VIEWBBC_KEY_RIGHT:
                editor->command_completion_pending = 0;
                if (viewbbc_editor_command_has_selection(editor))
                    editor->command_cursor = editor->command_selection_end;
                else if (editor->command_cursor < editor->command_length) editor->command_cursor++;
                viewbbc_editor_command_clear_selection(editor);
                break;
            case VIEWBBC_KEY_HOME:
                editor->command_completion_pending = 0;
                editor->command_cursor = 0;
                viewbbc_editor_command_clear_selection(editor);
                break;
            case VIEWBBC_KEY_END:
                editor->command_completion_pending = 0;
                editor->command_cursor = editor->command_length;
                viewbbc_editor_command_clear_selection(editor);
                break;
            case VIEWBBC_KEY_UP:
                editor->command_completion_pending = 0;
                viewbbc_editor_command_history_up(editor);
                break;
            case VIEWBBC_KEY_DOWN:
                editor->command_completion_pending = 0;
                viewbbc_editor_command_history_down(editor);
                break;
            case VIEWBBC_KEY_BACKSPACE:
                editor->command_completion_pending = 0;
                if (viewbbc_editor_command_delete_selection(editor)) break;
                if (editor->command_cursor > 0) {
                    memmove(editor->command_buffer + editor->command_cursor - 1u,
                            editor->command_buffer + editor->command_cursor,
                            editor->command_length - editor->command_cursor + 1u);
                    editor->command_cursor--;
                    editor->command_length--;
                }
                break;
            case VIEWBBC_KEY_DELETE:
                editor->command_completion_pending = 0;
                if (viewbbc_editor_command_delete_selection(editor)) break;
                if (editor->command_cursor < editor->command_length) {
                    memmove(editor->command_buffer + editor->command_cursor,
                            editor->command_buffer + editor->command_cursor + 1u,
                            editor->command_length - editor->command_cursor);
                    editor->command_length--;
                }
                break;
            case VIEWBBC_KEY_TAB:
                viewbbc_editor_command_complete(editor);
                break;
            case VIEWBBC_KEY_RETURN:
                viewbbc_editor_command_history_add(editor);
                viewbbc_editor_execute_command(editor);
                editor->command_length = 0;
                editor->command_cursor = 0;
                editor->command_buffer[0] = '\0';
                viewbbc_editor_command_clear_selection(editor);
                editor->command_history_browsing = 0;
                editor->command_history_draft_length = 0;
                editor->command_completion_pending = 0;
                break;
            default:
                break;
        }
        viewbbc_editor_render(editor);
        return;
    }

    if (editor->edit_command_active) {
        if (key == VIEWBBC_KEY_ESCAPE) {
            editor->edit_command_active = 0;
            editor->edit_command_length = 0;
            editor->edit_command_buffer[0] = '\0';
            editor->status_message[0] = '\0';
        } else if (key == VIEWBBC_KEY_BACKSPACE || key == VIEWBBC_KEY_DELETE) {
            if (editor->edit_command_length > 0u)
                editor->edit_command_buffer[--editor->edit_command_length] = '\0';
        } else if (key == VIEWBBC_KEY_RETURN) {
            if (editor->edit_command_length == 2u &&
                viewbbc_document_set_command(&editor->document, editor->cursor_line, editor->edit_command_buffer)) {
                editor->edit_command_active = 0;
                editor->edit_command_length = 0;
                editor->edit_command_buffer[0] = '\0';
                editor->file_modified = 1;
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "Edit command set");
            } else {
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "Enter two-letter command code");
            }
        }
        viewbbc_editor_render(editor);
        return;
    }

    switch (key) {
        case VIEWBBC_KEY_LEFT:
            if (editor->cursor_column > 0) editor->cursor_column--;
            break;
        case VIEWBBC_KEY_RIGHT:
            if (editor->cursor_column < current_line_length(editor)) editor->cursor_column++;
            break;
        case VIEWBBC_KEY_UP:
            if (editor->cursor_line > 0) editor->cursor_line--;
            clamp_column(editor);
            break;
        case VIEWBBC_KEY_DOWN:
            if (editor->cursor_line + 1 < viewbbc_document_line_count(&editor->document)) editor->cursor_line++;
            clamp_column(editor);
            break;
        case VIEWBBC_KEY_PAGE_UP:
            move_page(editor, -1);
            break;
        case VIEWBBC_KEY_PAGE_DOWN:
            move_page(editor, 1);
            break;
        case VIEWBBC_KEY_HOME:
        case VIEWBBC_KEY_F4:
            editor->cursor_column = 0;
            break;
        case VIEWBBC_KEY_END:
        case VIEWBBC_KEY_F5:
            editor->cursor_column = current_line_length(editor);
            break;
        case VIEWBBC_KEY_TAB: {
            size_t target = 0;
            if (viewbbc_ruler_next_tab_stop(editor->cursor_column, &target)) {
                size_t before_length = current_line_length(editor);
                if (target > before_length &&
                    !viewbbc_document_pad_line_to(&editor->document, editor->cursor_line, target)) {
                    (void)snprintf(editor->status_message, sizeof(editor->status_message), "No room");
                    break;
                }
                if (target > before_length) editor->file_modified = 1;
                editor->cursor_column = target;
            }
            break;
        }
        case VIEWBBC_KEY_BACKSPACE:
            if (editor->cursor_column > 0) {
                editor->cursor_column--;
                if (viewbbc_document_blank_char(&editor->document, editor->cursor_line, editor->cursor_column))
                    editor->file_modified = 1;
            }
            break;
        case VIEWBBC_KEY_DELETE:
        case VIEWBBC_KEY_F9:
            if (viewbbc_document_delete_char(&editor->document, editor->cursor_line, editor->cursor_column))
                editor->file_modified = 1;
            break;
        case VIEWBBC_KEY_RETURN:
            if (editor->cursor_line + 1 >= viewbbc_document_line_count(&editor->document)) {
                if (!viewbbc_document_insert_line(&editor->document, viewbbc_document_line_count(&editor->document))) {
                    (void)snprintf(editor->status_message, sizeof(editor->status_message), "No room");
                    break;
                }
                editor->file_modified = 1;
            }
            editor->cursor_line++;
            editor->cursor_column = 0;
            break;
        case VIEWBBC_KEY_F1:
            editor->cursor_line = 0;
            editor->cursor_column = 0;
            break;
        case VIEWBBC_KEY_F2:
            editor->cursor_line = viewbbc_document_line_count(&editor->document) - 1;
            editor->cursor_column = current_line_length(editor);
            break;
        case VIEWBBC_KEY_F3:
            if (viewbbc_document_delete_to_end(&editor->document, editor->cursor_line, editor->cursor_column))
                editor->file_modified = 1;
            break;
        case VIEWBBC_KEY_F6:
            if (viewbbc_document_insert_line(&editor->document, editor->cursor_line)) {
                editor->cursor_column = 0;
                editor->file_modified = 1;
            }
            else (void)snprintf(editor->status_message, sizeof(editor->status_message), "No room");
            break;
        case VIEWBBC_KEY_F7:
            if (viewbbc_document_delete_line(&editor->document, editor->cursor_line)) {
                editor->file_modified = 1;
                if (editor->cursor_line >= viewbbc_document_line_count(&editor->document)) {
                    editor->cursor_line = viewbbc_document_line_count(&editor->document) - 1;
                }
                clamp_column(editor);
            }
            break;
        case VIEWBBC_KEY_F0: {
            ViewBBCFormatResult result = viewbbc_format_block(&editor->document, &editor->markers,
                                                               editor->cursor_line, editor->format_mode,
                                                               editor->justify_mode,
                                                               &editor->cursor_line, &editor->cursor_column);
            if (result == VIEWBBC_FORMAT_OK) {
                editor->status_message[0] = '\0';
                editor->file_modified = 1;
            } else if (result == VIEWBBC_FORMAT_MODE_OFF) {
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "Format mode off");
            } else if (result == VIEWBBC_FORMAT_NO_MEMORY) {
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "No room");
            }
            break;
        }
        case VIEWBBC_KEY_F8:
            if (!viewbbc_document_insert_char(&editor->document, editor->cursor_line, editor->cursor_column, ' ')) {
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "No room");
            } else {
                editor->file_modified = 1;
            }
            break;
        case VIEWBBC_KEY_CTRL_F0: {
            ViewBBCBlockResult result = viewbbc_block_delete(&editor->document, &editor->markers,
                                                              &editor->cursor_line, &editor->cursor_column);
            set_block_status(editor, result);
            if (result == VIEWBBC_BLOCK_OK) editor->file_modified = 1;
            break;
        }
        case VIEWBBC_KEY_CTRL_F1:
            (void)viewbbc_view_next_match(editor);
            break;
        case VIEWBBC_KEY_CTRL_F2:
            editor->format_mode = !editor->format_mode;
            break;
        case VIEWBBC_KEY_CTRL_F3:
            editor->justify_mode = !editor->justify_mode;
            break;
        case VIEWBBC_KEY_INSERT:
        case VIEWBBC_KEY_CTRL_F4:
            editor->insert_mode = !editor->insert_mode;
            break;
        case VIEWBBC_KEY_CTRL_F6:
            if (viewbbc_document_split_line(&editor->document, editor->cursor_line, editor->cursor_column)) {
                editor->file_modified = 1;
                editor->cursor_line++;
                editor->cursor_column = 0;
            } else {
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "No room");
            }
            break;
        case VIEWBBC_KEY_CTRL_F7:
            if (viewbbc_document_join_with_next(&editor->document, editor->cursor_line))
                editor->file_modified = 1;
            break;
        case VIEWBBC_KEY_COPY: {
            ViewBBCBlockResult result = viewbbc_block_copy(&editor->document, &editor->markers,
                                                            editor->cursor_line, editor->cursor_column,
                                                            &editor->cursor_line, &editor->cursor_column);
            set_block_status(editor, result);
            if (result == VIEWBBC_BLOCK_OK) editor->file_modified = 1;
            break;
        }
        case VIEWBBC_KEY_SHIFT_F8:
            editor->edit_command_active = 1;
            editor->edit_command_length = 0;
            editor->edit_command_buffer[0] = '\0';
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Edit command: enter two letters, RETURN");
            break;
        case VIEWBBC_KEY_SHIFT_F9:
            if (viewbbc_document_delete_command(&editor->document, editor->cursor_line)) {
                editor->file_modified = 1;
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "Command deleted");
            } else {
                (void)snprintf(editor->status_message, sizeof(editor->status_message), "No command on this line");
            }
            break;
        case VIEWBBC_KEY_SHIFT_F0: {
            ViewBBCBlockResult result = viewbbc_block_move(&editor->document, &editor->markers,
                                                            editor->cursor_line, editor->cursor_column,
                                                            &editor->cursor_line, &editor->cursor_column);
            set_block_status(editor, result);
            if (result == VIEWBBC_BLOCK_OK) editor->file_modified = 1;
            break;
        }
        case VIEWBBC_KEY_SHIFT_F1: {
            size_t length = current_line_length(editor);
            if (editor->cursor_column < length) {
                uint8_t ch = viewbbc_document_char_at(&editor->document,
                                                       editor->cursor_line,
                                                       editor->cursor_column);
                uint8_t swapped = ch;
                if (ch >= 'a' && ch <= 'z') swapped = (uint8_t)(ch - ('a' - 'A'));
                else if (ch >= 'A' && ch <= 'Z') swapped = (uint8_t)(ch + ('a' - 'A'));
                if (swapped != ch && viewbbc_document_overwrite_char(&editor->document,
                                                                      editor->cursor_line,
                                                                      editor->cursor_column,
                                                                      swapped)) {
                    editor->file_modified = 1;
                }
                editor->cursor_column++;
            }
            break;
        }
        case VIEWBBC_KEY_SHIFT_F3:
            editor->delete_to_char_pending = 1;
            (void)snprintf(editor->status_message, sizeof(editor->status_message),
                           "Delete up to character");
            break;
        case VIEWBBC_KEY_SHIFT_F6:
            editor->marker_input = VIEWBBC_MARKER_INPUT_GOTO;
            break;
        case VIEWBBC_KEY_SHIFT_F7:
            editor->marker_input = VIEWBBC_MARKER_INPUT_SET;
            break;
        default:
            break;
    }

    keep_cursor_visible(editor);
    viewbbc_editor_render(editor);
}
