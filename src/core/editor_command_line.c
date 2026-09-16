#include "editor_command_line.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void command_clear_selection(ViewBBCEditor *editor);

static int command_ascii_ieq_n(const char *text, size_t length, const char *word) {
    size_t word_length = strlen(word);
    if (length != word_length) return 0;
    for (size_t i = 0; i < length; ++i) {
        if (toupper((unsigned char)text[i]) != toupper((unsigned char)word[i])) return 0;
    }
    return 1;
}


static void command_history_add(ViewBBCEditor *editor) {
    if (!editor || editor->command_length == 0) return;

    size_t first = 0;
    while (first < editor->command_length && isspace((unsigned char)editor->command_buffer[first])) ++first;
    if (first == editor->command_length) return;

    if (editor->command_history_count > 0 &&
        strcmp(editor->command_history[editor->command_history_count - 1u], editor->command_buffer) == 0)
        return;

    if (editor->command_history_count == VIEWBBC_COMMAND_HISTORY_MAX) {
        memmove(editor->command_history[0], editor->command_history[1],
                (VIEWBBC_COMMAND_HISTORY_MAX - 1u) * sizeof(editor->command_history[0]));
        editor->command_history_count--;
    }
    (void)snprintf(editor->command_history[editor->command_history_count],
                   sizeof(editor->command_history[0]), "%s", editor->command_buffer);
    editor->command_history_count++;
}

static void command_set_buffer(ViewBBCEditor *editor, const char *text) {
    if (!editor || !text) return;
    size_t n = strlen(text);
    if (n > VIEWBBC_COMMAND_MAX) n = VIEWBBC_COMMAND_MAX;
    memcpy(editor->command_buffer, text, n);
    editor->command_buffer[n] = '\0';
    editor->command_length = n;
    editor->command_cursor = n;
    editor->command_completion_pending = 0;
    command_clear_selection(editor);
}

static void command_history_up(ViewBBCEditor *editor) {
    if (!editor || editor->command_history_count == 0) return;
    if (!editor->command_history_browsing) {
        memcpy(editor->command_history_draft, editor->command_buffer, editor->command_length + 1u);
        editor->command_history_draft_length = editor->command_length;
        editor->command_history_index = editor->command_history_count - 1u;
        editor->command_history_browsing = 1;
    } else if (editor->command_history_index > 0) {
        editor->command_history_index--;
    }
    command_set_buffer(editor, editor->command_history[editor->command_history_index]);
    editor->command_history_browsing = 1;
}

static void command_history_down(ViewBBCEditor *editor) {
    if (!editor || !editor->command_history_browsing) return;
    if (editor->command_history_index + 1u < editor->command_history_count) {
        editor->command_history_index++;
        command_set_buffer(editor, editor->command_history[editor->command_history_index]);
        editor->command_history_browsing = 1;
        return;
    }
    command_set_buffer(editor, editor->command_history_draft);
    editor->command_history_draft_length = 0;
    editor->command_history_browsing = 0;
}

static int command_insert_bytes(ViewBBCEditor *editor, size_t at,
                                const char *text, size_t text_length) {
    if (!editor || !text || at > editor->command_length) return 0;
    if (text_length > VIEWBBC_COMMAND_MAX - editor->command_length) return 0;
    memmove(editor->command_buffer + at + text_length,
            editor->command_buffer + at,
            editor->command_length - at + 1u);
    memcpy(editor->command_buffer + at, text, text_length);
    editor->command_length += text_length;
    editor->command_cursor = at + text_length;
    command_clear_selection(editor);
    return 1;
}

static int command_replace_span(ViewBBCEditor *editor, size_t start, size_t end,
                                const char *replacement) {
    if (!editor || !replacement || start > end || end > editor->command_length) return 0;
    size_t replacement_length = strlen(replacement);
    size_t removed = end - start;
    if (replacement_length > VIEWBBC_COMMAND_MAX - (editor->command_length - removed)) return 0;
    memmove(editor->command_buffer + start + replacement_length,
            editor->command_buffer + end,
            editor->command_length - end + 1u);
    memcpy(editor->command_buffer + start, replacement, replacement_length);
    editor->command_length = editor->command_length - removed + replacement_length;
    editor->command_cursor = start + replacement_length;
    command_clear_selection(editor);
    return 1;
}

typedef enum {
    COMMAND_COMPLETE_NONE = 0,
    COMMAND_COMPLETE_NATIVE,
    COMMAND_COMPLETE_DFS
} CommandCompletionKind;

typedef struct {
    CommandCompletionKind kind;
    size_t replace_start;
    size_t replace_end;
    int quoted;
    char prefix[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
} CommandCompletionTarget;

static size_t command_common_prefix_ci(const char *a, const char *b);
static void command_show_completions(ViewBBCEditor *editor, const ViewBBCCommandOutput *matches);

static int command_complete_name(ViewBBCEditor *editor) {
    if (!editor || editor->command_cursor > editor->command_length) return 0;

    size_t start = 0u;
    while (start < editor->command_length && isspace((unsigned char)editor->command_buffer[start])) ++start;
    size_t end = start;
    while (end < editor->command_length && !isspace((unsigned char)editor->command_buffer[end])) ++end;

    /* Command completion applies only while the cursor is in the first token. */
    if (editor->command_cursor < start || editor->command_cursor > end) return 0;
    size_t prefix_length = editor->command_cursor - start;
    if (prefix_length == 0u) return 0;

    char common[VIEWBBC_COMMAND_MAX + 1u];
    common[0] = '\0';
    size_t matches = 0u;
    ViewBBCCommandOutput output;
    viewbbc_output_clear(&output);

    for (size_t i = 0u; i < viewbbc_command_name_count(); ++i) {
        const char *name = viewbbc_command_name_at(i);
        if (!name) continue;
        size_t j = 0u;
        while (j < prefix_length && name[j] &&
               toupper((unsigned char)name[j]) ==
               toupper((unsigned char)editor->command_buffer[start + j])) ++j;
        if (j != prefix_length) continue;
        if (matches == 0u) {
            (void)snprintf(common, sizeof(common), "%s", name);
        } else {
            size_t n = command_common_prefix_ci(common, name);
            common[n] = '\0';
        }
        ++matches;
        (void)viewbbc_output_add(&output, name);
    }

    if (matches == 0u) {
        editor->command_completion_pending = 0;
        return 1;
    }
    if (matches > 1u && editor->command_completion_pending) {
        command_show_completions(editor, &output);
        editor->command_completion_pending = 0;
        return 1;
    }

    if (!command_replace_span(editor, start, end, common)) {
        editor->command_completion_pending = 0;
        return 1;
    }
    if (matches == 1u && editor->command_cursor == editor->command_length)
        (void)command_insert_bytes(editor, editor->command_cursor, " ", 1u);
    editor->command_completion_pending = matches > 1u;
    return 1;
}

static int command_complete_config_argument(ViewBBCEditor *editor) {
    if (!editor || editor->command_cursor > editor->command_length) return 0;

    size_t p = 0u;
    while (p < editor->command_length && isspace((unsigned char)editor->command_buffer[p])) ++p;
    size_t command_start = p;
    while (p < editor->command_length && !isspace((unsigned char)editor->command_buffer[p])) ++p;
    size_t command_end = p;
    if (!command_ascii_ieq_n(editor->command_buffer + command_start,
                             command_end - command_start, "CONFIG")) return 0;
    if (command_end == editor->command_length) return 0;

    while (p < editor->command_length && isspace((unsigned char)editor->command_buffer[p])) ++p;
    size_t arg_start = p;
    while (p < editor->command_length && !isspace((unsigned char)editor->command_buffer[p])) ++p;
    size_t arg_end = p;
    if (editor->command_cursor < arg_start || editor->command_cursor > arg_end) return 0;
    if (p < editor->command_length) return 0; /* CONFIG has only one argument. */

    size_t prefix_length = editor->command_cursor - arg_start;
    if (prefix_length == 0u) return 0;
    static const char *const args[] = { "LOC", "RESET" };
    char common[16] = "";
    size_t matches = 0u;
    ViewBBCCommandOutput output;
    viewbbc_output_clear(&output);

    for (size_t i = 0u; i < sizeof(args) / sizeof(args[0]); ++i) {
        size_t j = 0u;
        while (j < prefix_length && args[i][j] &&
               toupper((unsigned char)args[i][j]) ==
               toupper((unsigned char)editor->command_buffer[arg_start + j])) ++j;
        if (j != prefix_length) continue;
        if (matches == 0u) (void)snprintf(common, sizeof(common), "%s", args[i]);
        else common[command_common_prefix_ci(common, args[i])] = '\0';
        ++matches;
        (void)viewbbc_output_add(&output, args[i]);
    }

    if (matches == 0u) {
        editor->command_completion_pending = 0;
        return 1;
    }
    if (matches > 1u && editor->command_completion_pending) {
        command_show_completions(editor, &output);
        editor->command_completion_pending = 0;
        return 1;
    }
    if (!command_replace_span(editor, arg_start, arg_end, common)) {
        editor->command_completion_pending = 0;
        return 1;
    }
    if (matches == 1u && editor->command_cursor == editor->command_length)
        (void)command_insert_bytes(editor, editor->command_cursor, " ", 1u);
    editor->command_completion_pending = matches > 1u;
    return 1;
}

static int command_completion_target(const ViewBBCEditor *editor, CommandCompletionTarget *target) {
    if (!editor || !target || editor->command_cursor > editor->command_length) return 0;
    *target = (CommandCompletionTarget){0};

    size_t p = 0;
    while (p < editor->command_length && isspace((unsigned char)editor->command_buffer[p])) ++p;
    size_t command_start = p;
    while (p < editor->command_length && !isspace((unsigned char)editor->command_buffer[p])) ++p;
    size_t command_end = p;
    if (command_end == command_start || editor->command_cursor < command_end) return 0;
    if (command_end == editor->command_length) return 0; /* file arguments start after command whitespace */

    int is_load = command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "LOAD") ||
                  command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "L");
    int is_save = command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "SAVE");
    int is_read = command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "READ");
    int is_write = command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "WRITE");
    int is_export = command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "EXPORT");
    int is_mount = command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "MOUNT");
    int is_cd = command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "CD");
    int is_print = command_ascii_ieq_n(editor->command_buffer + command_start, command_end - command_start, "PRINT");
    if (!is_load && !is_save && !is_read && !is_write && !is_export && !is_mount && !is_cd && !is_print)
        return 0;

    while (p < editor->command_length && isspace((unsigned char)editor->command_buffer[p])) ++p;
    if (p > editor->command_cursor) return 0;

    if (is_mount) {
        size_t token_start = p;
        while (p < editor->command_length && !isspace((unsigned char)editor->command_buffer[p])) ++p;
        size_t token_length = p - token_start;
        if (command_ascii_ieq_n(editor->command_buffer + token_start, token_length, "SSD") ||
            command_ascii_ieq_n(editor->command_buffer + token_start, token_length, "DSD")) {
            if (p == editor->command_length) return 0;
            while (p < editor->command_length && isspace((unsigned char)editor->command_buffer[p])) ++p;
            if (p > editor->command_cursor) return 0;
        } else {
            p = token_start;
        }
    }

    if (is_print) {
        static const char *prefixes[] = { "file:", "text:", "pdf:", "odt:" };
        size_t left = editor->command_length - p;
        size_t matched = 0u;
        for (size_t k = 0; k < sizeof(prefixes) / sizeof(prefixes[0]); ++k) {
            size_t n = strlen(prefixes[k]);
            if (left < n) continue;
            size_t i = 0u;
            for (; i < n; ++i)
                if (tolower((unsigned char)editor->command_buffer[p + i]) != prefixes[k][i]) break;
            if (i == n) { matched = n; break; }
        }
        if (matched == 0u) return 0;
        p += matched;
        if (p > editor->command_cursor) return 0;
    }

    size_t raw_start = p;
    int quoted = p < editor->command_length && editor->command_buffer[p] == '"';
    if (quoted) ++p;
    size_t token_start = p;
    size_t token_end = p;
    if (quoted) {
        while (token_end < editor->command_length && editor->command_buffer[token_end] != '"') ++token_end;
    } else {
        while (token_end < editor->command_length && !isspace((unsigned char)editor->command_buffer[token_end])) ++token_end;
    }
    if (editor->command_cursor < token_start || editor->command_cursor > token_end) return 0;

    size_t prefix_length = editor->command_cursor - token_start;
    if (prefix_length > VIEWBBC_COMMAND_ARGUMENT_MAX) return 0;
    memcpy(target->prefix, editor->command_buffer + token_start, prefix_length);
    target->prefix[prefix_length] = '\0';
    target->replace_start = quoted ? token_start : raw_start;
    target->replace_end = token_end;
    target->quoted = quoted;

    if (editor->image_mounted && (is_load || is_read)) target->kind = COMMAND_COMPLETE_DFS;
    else if (editor->image_mounted && is_cd) return 0;
    else target->kind = COMMAND_COMPLETE_NATIVE;
    return 1;
}

static size_t command_common_prefix_ci(const char *a, const char *b) {
    size_t n = 0;
    while (a[n] && b[n] && toupper((unsigned char)a[n]) == toupper((unsigned char)b[n])) ++n;
    return n;
}

static int command_complete_dfs(const ViewBBCEditor *editor, const char *prefix,
                                char *completion, size_t completion_size,
                                size_t *match_count, ViewBBCCommandOutput *matches) {
    if (!editor || !prefix || !completion || completion_size == 0 || !match_count || !matches) return 0;
    viewbbc_output_clear(matches);
    *match_count = 0;
    completion[0] = '\0';
    size_t prefix_length = strlen(prefix);

    for (int side = 0; side < editor->mounted_image.side_count; ++side) {
        const ViewBBCDFSSide *disc_side = &editor->mounted_image.sides[side];
        for (size_t i = 0; i < disc_side->file_count; ++i) {
            const ViewBBCDFSFile *file = &disc_side->files[i];
            if (toupper((unsigned char)file->name[0]) !=
                toupper((unsigned char)editor->filesystem.dfs_directory)) continue;
            const char *name = file->name + 2;
            size_t j = 0;
            while (j < prefix_length && name[j] &&
                   toupper((unsigned char)name[j]) == toupper((unsigned char)prefix[j])) ++j;
            if (j != prefix_length) continue;
            if (*match_count == 0) {
                (void)snprintf(completion, completion_size, "%s", name);
            } else {
                size_t common = command_common_prefix_ci(completion, name);
                completion[common] = '\0';
            }
            (*match_count)++;
            (void)viewbbc_output_add(matches, name);
        }
    }
    return 1;
}

static void command_show_completions(ViewBBCEditor *editor, const ViewBBCCommandOutput *matches) {
    if (!editor || !matches) return;
    for (size_t i = 0; i < matches->line_count; ++i)
        (void)viewbbc_console_add(&editor->console, matches->lines[i]);
    if (matches->truncated) (void)viewbbc_console_add(&editor->console, "...");
}

static void command_complete(ViewBBCEditor *editor) {
    if (!editor) return;
    if (command_complete_name(editor)) return;
    if (command_complete_config_argument(editor)) return;
    CommandCompletionTarget target;
    if (!command_completion_target(editor, &target)) {
        editor->command_completion_pending = 0;
        return;
    }

    char completion[VIEWBBC_PATH_LIMIT];
    size_t match_count = 0;
    int unique_is_directory = 0;
    ViewBBCCommandOutput matches;
    viewbbc_output_clear(&matches);
    int ok = target.kind == COMMAND_COMPLETE_DFS
        ? command_complete_dfs(editor, target.prefix, completion, sizeof(completion), &match_count, &matches)
        : viewbbc_fs_complete_native(&editor->filesystem, target.prefix, completion, sizeof(completion),
                                     &match_count, &unique_is_directory, &matches);
    if (!ok || match_count == 0) {
        editor->command_completion_pending = 0;
        return;
    }

    if (match_count > 1u && editor->command_completion_pending) {
        command_show_completions(editor, &matches);
        editor->command_completion_pending = 0;
        return;
    }

    char replacement[VIEWBBC_PATH_LIMIT + 4];
    const char *result = completion;
    int need_quote = !target.quoted && strpbrk(result, " \t") != NULL;
    int unique = match_count == 1u;
    if (target.kind == COMMAND_COMPLETE_NATIVE && unique && unique_is_directory) {
        size_t n = strlen(completion);
        if (n + 1u < sizeof(completion) && (n == 0 || completion[n - 1u] != '/')) {
            completion[n++] = '/';
            completion[n] = '\0';
        }
    }

    if (target.quoted) {
        (void)snprintf(replacement, sizeof(replacement), "%s", completion);
    } else if (need_quote) {
        if (unique && !(target.kind == COMMAND_COMPLETE_NATIVE && unique_is_directory))
            (void)snprintf(replacement, sizeof(replacement), "\"%s\"", completion);
        else
            (void)snprintf(replacement, sizeof(replacement), "\"%s", completion);
    } else {
        (void)snprintf(replacement, sizeof(replacement), "%s", completion);
    }

    if (command_replace_span(editor, target.replace_start, target.replace_end, replacement)) {
        if (unique && !(target.kind == COMMAND_COMPLETE_NATIVE && unique_is_directory) &&
            editor->command_cursor == editor->command_length) {
            if (target.quoted) {
                if (editor->command_cursor >= editor->command_length ||
                    editor->command_buffer[editor->command_cursor] != '"') {
                    (void)command_insert_bytes(editor, editor->command_cursor, "\"", 1u);
                }
            }
            (void)command_insert_bytes(editor, editor->command_cursor, " ", 1u);
        }
    }
    editor->command_completion_pending = match_count > 1u;
}


static void command_clear_selection(ViewBBCEditor *editor) {
    if (!editor) return;
    editor->command_selection_active = 0;
    editor->command_selection_start = editor->command_cursor;
    editor->command_selection_end = editor->command_cursor;
}

static void command_update_selection(ViewBBCEditor *editor, size_t cursor) {
    if (!editor) return;
    if (cursor > editor->command_length) cursor = editor->command_length;
    editor->command_cursor = cursor;
    size_t a = editor->command_selection_anchor;
    if (a > editor->command_length) a = editor->command_length;
    editor->command_selection_start = a < cursor ? a : cursor;
    editor->command_selection_end = a < cursor ? cursor : a;
    editor->command_selection_active = editor->command_selection_start < editor->command_selection_end;
}

static int command_prompt_row(const ViewBBCEditor *editor) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND || editor->output_paging) return -1;
    for (int y = editor->screen.rows - 1; y >= 0; --y) {
        if (viewbbc_screen_get(&editor->screen, 0, y).ch == '=' &&
            viewbbc_screen_get(&editor->screen, 1, y).ch == '>') return y;
    }
    return -1;
}

size_t viewbbc_editor_command_visible_start(const ViewBBCEditor *editor) {
    if (!editor) return 0u;
    int available = editor->screen.cols - 2;
    if (available <= 0) return 0u;
    size_t shown = (size_t)available;
    size_t start = 0u;
    if (editor->command_cursor >= shown) start = editor->command_cursor - shown + 1u;
    if (start > editor->command_length) start = editor->command_length;
    return start;
}

static size_t command_index_from_screen(const ViewBBCEditor *editor, int screen_x, int screen_y) {
    int row = command_prompt_row(editor);
    if (row < 0 || screen_y != row) return SIZE_MAX;
    size_t start = viewbbc_editor_command_visible_start(editor);
    if (screen_x <= 2) return start;
    size_t offset = (size_t)(screen_x - 2);
    size_t index = start + offset;
    return index > editor->command_length ? editor->command_length : index;
}

void viewbbc_editor_command_clear_selection(ViewBBCEditor *editor) { command_clear_selection(editor); }

int viewbbc_editor_command_has_selection(const ViewBBCEditor *editor) {
    return editor && editor->command_selection_active &&
           editor->command_selection_start < editor->command_selection_end &&
           editor->command_selection_end <= editor->command_length;
}

int viewbbc_editor_command_delete_selection(ViewBBCEditor *editor) {
    if (!viewbbc_editor_command_has_selection(editor)) return 0;
    size_t start = editor->command_selection_start;
    size_t end = editor->command_selection_end;
    memmove(editor->command_buffer + start, editor->command_buffer + end,
            editor->command_length - end + 1u);
    editor->command_length -= end - start;
    editor->command_cursor = start;
    command_clear_selection(editor);
    editor->command_completion_pending = 0;
    return 1;
}

void viewbbc_editor_command_select_left(ViewBBCEditor *editor) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND || editor->output_paging) return;
    if (!editor->command_selection_active) editor->command_selection_anchor = editor->command_cursor;
    if (editor->command_cursor > 0u) command_update_selection(editor, editor->command_cursor - 1u);
    editor->command_completion_pending = 0;
}

void viewbbc_editor_command_select_right(ViewBBCEditor *editor) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND || editor->output_paging) return;
    if (!editor->command_selection_active) editor->command_selection_anchor = editor->command_cursor;
    if (editor->command_cursor < editor->command_length) command_update_selection(editor, editor->command_cursor + 1u);
    editor->command_completion_pending = 0;
}

void viewbbc_editor_command_select_all(ViewBBCEditor *editor) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND || editor->output_paging) return;
    editor->command_selection_anchor = 0u;
    editor->command_selection_start = 0u;
    editor->command_selection_end = editor->command_length;
    editor->command_selection_active = editor->command_length != 0u;
    editor->command_cursor = editor->command_length;
}

void viewbbc_editor_command_mouse_down(ViewBBCEditor *editor, int screen_x, int screen_y) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND) return;
    size_t index = command_index_from_screen(editor, screen_x, screen_y);
    if (index == SIZE_MAX) return;
    editor->command_mouse_dragging = 1;
    editor->command_selection_anchor = index;
    editor->command_cursor = index;
    command_clear_selection(editor);
    viewbbc_editor_render(editor);
}

void viewbbc_editor_command_mouse_drag(ViewBBCEditor *editor, int screen_x, int screen_y) {
    if (!editor || !editor->command_mouse_dragging) return;
    int row = command_prompt_row(editor);
    if (row < 0) return;
    if (screen_y != row) screen_y = row;
    size_t index = command_index_from_screen(editor, screen_x, screen_y);
    if (index == SIZE_MAX) return;
    command_update_selection(editor, index);
    viewbbc_editor_render(editor);
}

void viewbbc_editor_command_mouse_up(ViewBBCEditor *editor, int screen_x, int screen_y) {
    if (!editor || !editor->command_mouse_dragging) return;
    viewbbc_editor_command_mouse_drag(editor, screen_x, screen_y);
    editor->command_mouse_dragging = 0;
}


void viewbbc_editor_command_reset_navigation(ViewBBCEditor *editor) {
    if (!editor) return;
    editor->command_completion_pending = 0;
    editor->command_history_browsing = 0;
}
void viewbbc_editor_command_history_add(ViewBBCEditor *editor) { command_history_add(editor); }
void viewbbc_editor_command_history_up(ViewBBCEditor *editor) { command_history_up(editor); }
void viewbbc_editor_command_history_down(ViewBBCEditor *editor) { command_history_down(editor); }
int viewbbc_editor_command_insert_bytes(ViewBBCEditor *editor, size_t at, const char *text, size_t length) { return command_insert_bytes(editor, at, text, length); }
void viewbbc_editor_command_complete(ViewBBCEditor *editor) { command_complete(editor); }
