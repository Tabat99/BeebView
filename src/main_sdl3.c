#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__linux__)
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "viewbbc/editor.h"
#include "viewbbc/editor_mouse.h"
#include "viewbbc/config.h"
#include "viewbbc/print.h"
#include "viewbbc/startup.h"
#include "frontend/sdl3/sdl3_frontend.h"
#include "core/editor_command_line.h"


static int command_prompt_row(const ViewBBCEditor *editor) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND || editor->output_paging) return -1;
    for (int y = editor->screen.rows - 1; y >= 0; --y) {
        if (viewbbc_screen_get(&editor->screen, 0, y).ch == '=' &&
            viewbbc_screen_get(&editor->screen, 1, y).ch == '>') return y;
    }
    return -1;
}

static size_t command_view_cell(const ViewBBCEditor *editor, int x, int y) {
    if (!editor || editor->screen.cols <= 0 || editor->screen.rows <= 0) return 0u;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= editor->screen.cols) x = editor->screen.cols - 1;
    if (y >= editor->screen.rows) y = editor->screen.rows - 1;
    return (size_t)y * (size_t)editor->screen.cols + (size_t)x;
}

static void command_view_clear_selection(ViewBBCEditor *editor) {
    if (!editor) return;
    editor->command_view_dragging = 0;
    editor->command_view_selection_active = 0;
    editor->command_view_selection_start = 0u;
    editor->command_view_selection_end = 0u;
}

static void command_view_mouse_down(ViewBBCEditor *editor, int x, int y) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND) return;
    viewbbc_editor_command_clear_selection(editor);
    editor->command_view_selection_anchor = command_view_cell(editor, x, y);
    editor->command_view_selection_start = editor->command_view_selection_anchor;
    editor->command_view_selection_end = editor->command_view_selection_anchor;
    editor->command_view_selection_active = 0;
    editor->command_view_dragging = 1;
    viewbbc_editor_render(editor);
}

static void command_view_mouse_drag(ViewBBCEditor *editor, int x, int y) {
    if (!editor || !editor->command_view_dragging) return;
    size_t cell = command_view_cell(editor, x, y);
    size_t anchor = editor->command_view_selection_anchor;
    if (cell >= anchor) {
        editor->command_view_selection_start = anchor;
        editor->command_view_selection_end = cell + 1u;
    } else {
        editor->command_view_selection_start = cell;
        editor->command_view_selection_end = anchor + 1u;
    }
    editor->command_view_selection_active = cell != anchor;
    viewbbc_editor_render(editor);
}

static void command_view_mouse_up(ViewBBCEditor *editor, int x, int y) {
    if (!editor || !editor->command_view_dragging) return;
    command_view_mouse_drag(editor, x, y);
    editor->command_view_dragging = 0;
}

static char *selected_command_view_text(const ViewBBCEditor *editor) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND ||
        !editor->command_view_selection_active || editor->screen.cols <= 0) return NULL;
    size_t start = editor->command_view_selection_start;
    size_t end = editor->command_view_selection_end;
    size_t cells = (size_t)editor->screen.cols * (size_t)editor->screen.rows;
    if (start >= end || start >= cells) return NULL;
    if (end > cells) end = cells;

    size_t first_row = start / (size_t)editor->screen.cols;
    size_t last_row = (end - 1u) / (size_t)editor->screen.cols;
    size_t capacity = end - start + (last_row - first_row) + 1u;
    char *text = malloc(capacity);
    if (!text) return NULL;
    size_t out = 0u;

    for (size_t row = first_row; row <= last_row; ++row) {
        size_t row_begin = row * (size_t)editor->screen.cols;
        size_t from = start > row_begin ? start - row_begin : 0u;
        size_t to = end < row_begin + (size_t)editor->screen.cols
            ? end - row_begin : (size_t)editor->screen.cols;
        size_t line_out = out;
        for (size_t col = from; col < to; ++col) {
            uint32_t ch = viewbbc_screen_get(&editor->screen, (int)col, (int)row).ch;
            text[out++] = ch >= 32u && ch <= 255u ? (char)ch : ' ';
        }
        while (out > line_out && text[out - 1u] == ' ') --out;
        if (row < last_row) text[out++] = '\n';
    }
    text[out] = '\0';
    return text;
}

static char *selected_command_text(const ViewBBCEditor *editor) {
    if (!editor || editor->mode != VIEWBBC_MODE_COMMAND ||
        !viewbbc_editor_command_has_selection(editor)) return NULL;
    size_t start = editor->command_selection_start;
    size_t end = editor->command_selection_end;
    size_t length = end - start;
    char *text = malloc(length + 1u);
    if (!text) return NULL;
    memcpy(text, editor->command_buffer + start, length);
    text[length] = '\0';
    return text;
}


static char *selected_text(const ViewBBCEditor *editor) {
    if (!editor || !editor->mouse_selection_active) return NULL;

    size_t sl = 0, sc = 0, el = 0, ec = 0;
    if (!viewbbc_markers_get(&editor->markers, 1u, &sl, &sc) ||
        !viewbbc_markers_get(&editor->markers, 2u, &el, &ec)) return NULL;
    if (sl > el || (sl == el && sc >= ec) || el >= viewbbc_document_line_count(&editor->document))
        return NULL;

    size_t total = 0;
    for (size_t line = sl; line <= el; ++line) {
        size_t len = viewbbc_document_line_length(&editor->document, line);
        size_t begin = line == sl ? sc : 0u;
        size_t end = line == el ? ec : len;
        if (begin > len) begin = len;
        if (end > len) end = len;
        if (end > begin) {
            if (total > SIZE_MAX - (end - begin)) return NULL;
            total += end - begin;
        }
        if (line < el) {
            if (total == SIZE_MAX) return NULL;
            ++total;
        }
    }

    char *text = malloc(total + 1u);
    if (!text) return NULL;
    size_t out = 0;
    for (size_t line = sl; line <= el; ++line) {
        size_t len = viewbbc_document_line_length(&editor->document, line);
        size_t begin = line == sl ? sc : 0u;
        size_t end = line == el ? ec : len;
        if (begin > len) begin = len;
        if (end > len) end = len;
        for (size_t col = begin; col < end; ++col)
            text[out++] = (char)viewbbc_document_char_at(&editor->document, line, col);
        if (line < el) text[out++] = '\n';
    }
    text[out] = '\0';
    return text;
}

static void copy_selection(ViewBBCEditor *editor, int primary) {
    char *text = NULL;
    if (editor && editor->mode == VIEWBBC_MODE_COMMAND) {
        text = editor->command_view_selection_active
            ? selected_command_view_text(editor) : selected_command_text(editor);
    } else {
        text = selected_text(editor);
    }
    if (!text) {
        snprintf(editor->status_message, sizeof(editor->status_message), "No selection");
        viewbbc_editor_render(editor);
        return;
    }
    int ok = primary ? viewbbc_sdl3_set_primary_selection_text(text)
                     : viewbbc_sdl3_set_clipboard_text(text);
    free(text);
    if (!primary) {
        snprintf(editor->status_message, sizeof(editor->status_message),
                 ok ? "Copied to clipboard" : "Clipboard copy failed");
        viewbbc_editor_render(editor);
    }
}

static void delete_selection(ViewBBCEditor *editor) {
    if (!editor) return;
    if (editor->mode == VIEWBBC_MODE_COMMAND) {
        if (viewbbc_editor_command_delete_selection(editor)) viewbbc_editor_render(editor);
        return;
    }
    if (!editor->mouse_selection_active || editor->mode != VIEWBBC_MODE_TEXT) return;
    viewbbc_editor_handle_key(editor, VIEWBBC_KEY_CTRL_F0);
    editor->mouse_selection_active = 0;
    editor->mouse_dragging = 0;
    viewbbc_editor_render(editor);
}

static void cut_selection(ViewBBCEditor *editor) {
    if (editor && editor->mode == VIEWBBC_MODE_COMMAND && editor->command_view_selection_active) {
        copy_selection(editor, 0);
        return;
    }
    int selected = editor && ((editor->mode == VIEWBBC_MODE_COMMAND &&
                               viewbbc_editor_command_has_selection(editor)) ||
                              (editor->mode == VIEWBBC_MODE_TEXT &&
                               editor->mouse_selection_active));
    if (!selected) {
        if (editor) {
            snprintf(editor->status_message, sizeof(editor->status_message), "No selection");
            viewbbc_editor_render(editor);
        }
        return;
    }
    copy_selection(editor, 0);
    delete_selection(editor);
}

static void paste_text(ViewBBCEditor *editor, const char *text) {
    if (!editor || !text || editor->mode != VIEWBBC_MODE_TEXT) return;
    if (editor->mouse_selection_active) delete_selection(editor);
    for (size_t i = 0; text[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\r') {
            if (text[i + 1] == '\n') ++i;
            viewbbc_editor_handle_key(editor, VIEWBBC_KEY_RETURN);
        } else if (c == '\n') {
            viewbbc_editor_handle_key(editor, VIEWBBC_KEY_RETURN);
        } else if (c == '\t') {
            viewbbc_editor_handle_key(editor, VIEWBBC_KEY_TAB);
        } else if (c >= 32u && c <= 126u) {
            viewbbc_editor_insert_char(editor, c);
        }
    }
}

static void handle_print_request(ViewBBCEditor *editor, const ViewBBCConfig *config) {
    if (!editor || !config || !editor->print_requested) return;
    editor->print_requested = 0;
    const char *target = editor->print_target_override[0]
        ? editor->print_target_override : config->print_target;
    char chosen[VIEWBBC_CONFIG_VALUE_MAX + 32u];
    if (strcmp(target, "file") == 0) {
        if (!viewbbc_sdl3_choose_print_file(chosen, sizeof(chosen))) {
            snprintf(editor->status_message, sizeof(editor->status_message), "Print cancelled");
            editor->print_target_override[0] = '\0';
            viewbbc_editor_render(editor);
            return;
        }
        target = chosen;
    }
    (void)viewbbc_print_document(&editor->document, target,
                                 editor->status_message, sizeof(editor->status_message));
    editor->print_target_override[0] = '\0';
    viewbbc_editor_render(editor);
}

static void paste_command_text(ViewBBCEditor *editor, const char *text) {
    if (!editor || !text || editor->mode != VIEWBBC_MODE_COMMAND || editor->output_paging) return;
    (void)viewbbc_editor_command_delete_selection(editor);
    char clean[VIEWBBC_COMMAND_MAX + 1u];
    size_t out = 0u;
    int pending_space = 0;
    for (size_t i = 0u; text[i] != '\0' && out < VIEWBBC_COMMAND_MAX; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '\r' || c == '\n' || c == '\t') {
            pending_space = 1;
            if (c == '\r' && text[i + 1u] == '\n') ++i;
            continue;
        }
        if (c < 32u || c > 126u) continue;
        if (pending_space && out > 0u && clean[out - 1u] != ' ' && out < VIEWBBC_COMMAND_MAX)
            clean[out++] = ' ';
        pending_space = 0;
        if (out < VIEWBBC_COMMAND_MAX) clean[out++] = (char)c;
    }
    clean[out] = '\0';
    size_t room = VIEWBBC_COMMAND_MAX - editor->command_length;
    if (out > room) out = room;
    if (out > 0u) (void)viewbbc_editor_command_insert_bytes(editor, editor->command_cursor, clean, out);
    viewbbc_editor_render(editor);
}

static void paste_clipboard(ViewBBCEditor *editor, int primary) {
    if (!editor || (editor->mode != VIEWBBC_MODE_TEXT && editor->mode != VIEWBBC_MODE_COMMAND)) return;
    char *text = primary ? viewbbc_sdl3_get_primary_selection_text()
                         : viewbbc_sdl3_get_clipboard_text();
    if (!text) {
        snprintf(editor->status_message, sizeof(editor->status_message), "Clipboard unavailable");
        viewbbc_editor_render(editor);
        return;
    }
    if (editor->mode == VIEWBBC_MODE_COMMAND) paste_command_text(editor, text);
    else paste_text(editor, text);
    viewbbc_sdl3_free_text(text);
}


#if defined(__linux__)
static int ensure_directory(const char *path) {
    char copy[4096];
    size_t length;
    if (!path) return 0;
    length = strlen(path);
    if (length == 0u || length >= sizeof(copy)) return 0;
    memcpy(copy, path, length + 1u);
    for (char *p = copy + 1; *p; ++p) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(copy, 0700) != 0 && errno != EEXIST) return 0;
        *p = '/';
    }
    return mkdir(copy, 0700) == 0 || errno == EEXIST;
}

static void run_desktop_refresh(const char *program, const char *arg) {
    pid_t pid = fork();
    if (pid != 0) {
        if (pid > 0) { int status; (void)waitpid(pid, &status, 0); }
        return;
    }
    execlp(program, program, arg, (char *)NULL);
    _exit(127);
}

static int copy_regular_file(const char *source, const char *target) {
    unsigned char buffer[16384];
    FILE *src = fopen(source, "rb");
    FILE *dst;
    size_t got;
    int ok = 1;
    if (!src) return 0;
    dst = fopen(target, "wb");
    if (!dst) { fclose(src); return 0; }
    while ((got = fread(buffer, 1u, sizeof(buffer), src)) != 0u) {
        if (fwrite(buffer, 1u, got, dst) != got) { ok = 0; break; }
    }
    if (ferror(src) || fflush(dst) != 0) ok = 0;
    if (fclose(dst) != 0) ok = 0;
    fclose(src);
    if (!ok) (void)remove(target);
    return ok;
}

static int write_exec_quoted(FILE *file, const char *value) {
    if (fputc('"', file) == EOF) return 0;
    for (const unsigned char *p = (const unsigned char *)value; *p; ++p) {
        if (*p == '"' || *p == '`' || *p == '$' || *p == '\\') {
            if (fputc('\\', file) == EOF) return 0;
        }
        if (fputc((int)*p, file) == EOF) return 0;
    }
    return fputc('"', file) != EOF;
}

/* Standalone AppImages are not installed by a package manager, so their
   embedded desktop file is not necessarily visible to the desktop.  On first
   graphical launch, register this exact AppImage as a per-user text/plain
   handler.  Re-running after moving the AppImage refreshes the Exec path. */
static void register_running_appimage(void) {
    const char *appimage = getenv("APPIMAGE");
    const char *home;
    const char *xdg_data_home;
    char applications[4096];
    char desktop_path[4096];
    char temp_path[4096];
    char icon_dir[4096];
    char icon_path[4096];
    char source_icon[4096];
    const char *appdir;
    FILE *file;

    if (!appimage || appimage[0] != '/') return;
    xdg_data_home = getenv("XDG_DATA_HOME");
    if (xdg_data_home && xdg_data_home[0] == '/') {
        if (snprintf(applications, sizeof(applications), "%s/applications", xdg_data_home) >= (int)sizeof(applications)) return;
    } else {
        home = getenv("HOME");
        if (!home || home[0] != '/') return;
        if (snprintf(applications, sizeof(applications), "%s/.local/share/applications", home) >= (int)sizeof(applications)) return;
    }
    if (!ensure_directory(applications)) return;

    /* The AppImage filesystem disappears when the process exits, so copy
       its packaged icon into the user's persistent hicolor theme.  Use the
       icon theme name in the desktop entry: menus/Open-With dialogs resolve
       Icon=beebview more consistently than an arbitrary absolute pathname. */
    appdir = getenv("APPDIR");
    icon_path[0] = '\0';
    if (snprintf(icon_dir, sizeof(icon_dir), "%.*s/icons/hicolor/256x256/apps",
                 (int)(strlen(applications) - strlen("/applications")), applications) < (int)sizeof(icon_dir) &&
        ensure_directory(icon_dir) &&
        snprintf(icon_path, sizeof(icon_path), "%s/beebview.png", icon_dir) < (int)sizeof(icon_path) &&
        appdir && appdir[0] == '/') {
        const char *relative_icons[] = {
            "/usr/share/icons/hicolor/256x256/apps/beebview.png",
            "/beebview.png",
            "/.DirIcon"
        };
        int copied = 0;
        for (size_t i = 0; i < sizeof(relative_icons) / sizeof(relative_icons[0]); ++i) {
            if (snprintf(source_icon, sizeof(source_icon), "%s%s", appdir, relative_icons[i]) >= (int)sizeof(source_icon)) continue;
            if (copy_regular_file(source_icon, icon_path)) { copied = 1; break; }
        }
        if (!copied) icon_path[0] = '\0';

        /* Also install the scalable icon when the AppImage contains it. */
        if (copied) {
            char scalable_dir[4096];
            char scalable_target[4096];
            if (snprintf(scalable_dir, sizeof(scalable_dir), "%.*s/icons/hicolor/scalable/apps",
                         (int)(strlen(applications) - strlen("/applications")), applications) < (int)sizeof(scalable_dir) &&
                ensure_directory(scalable_dir) &&
                snprintf(scalable_target, sizeof(scalable_target), "%s/beebview.svg", scalable_dir) < (int)sizeof(scalable_target) &&
                snprintf(source_icon, sizeof(source_icon), "%s/usr/share/icons/hicolor/scalable/apps/beebview.svg", appdir) < (int)sizeof(source_icon)) {
                (void)copy_regular_file(source_icon, scalable_target);
            }
        }
    }

    if (snprintf(desktop_path, sizeof(desktop_path), "%s/beebview-appimage.desktop", applications) >= (int)sizeof(desktop_path)) return;
    if (snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld", desktop_path, (long)getpid()) >= (int)sizeof(temp_path)) return;

    file = fopen(temp_path, "w");
    if (!file) return;
    int write_ok = fputs("[Desktop Entry]\nType=Application\nName=BeebView\n"
                         "Comment=Native reimplementation of Acornsoft VIEW for the BBC Micro\nExec=", file) != EOF &&
                   write_exec_quoted(file, appimage) &&
                   fputs(" %f\nIcon=beebview\nTerminal=false\nCategories=Utility;TextEditor;\n"
                         "MimeType=text/plain;text/x-log;text/x-csrc;text/x-chdr;text/x-c++src;text/x-c++hdr;application/x-shellscript;text/x-python;text/markdown;text/csv;application/json;application/xml;text/xml;text/html;\nStartupNotify=true\n", file) != EOF &&
                   fflush(file) == 0;
    if (fclose(file) != 0) write_ok = 0;
    if (!write_ok) {
        remove(temp_path);
        return;
    }
    (void)chmod(temp_path, 0644);
    if (rename(temp_path, desktop_path) != 0) {
        remove(temp_path);
        return;
    }

    /* Desktop environments build MIME-handler and icon caches from installed
       entries.  A package manager normally refreshes these; a standalone
       AppImage must do it itself after creating/updating its user entry. */
    run_desktop_refresh("update-desktop-database", applications);
    if (icon_path[0] != '\0') {
        char hicolor[4096];
        size_t suffix = strlen("/256x256/apps/beebview.png");
        size_t n = strlen(icon_path);
        if (n > suffix && n - suffix < sizeof(hicolor)) {
            memcpy(hicolor, icon_path, n - suffix);
            hicolor[n - suffix] = '\0';
            run_desktop_refresh("gtk-update-icon-cache", hicolor);
        }
    }
}
#endif

int main(int argc, char *argv[]) {
#if defined(__linux__)
    register_running_appimage();
#endif
    ViewBBCEditor editor;
    ViewBBCConfig config;
    ViewBBCStartupOptions startup;
    char startup_error[256];

    if (!viewbbc_startup_parse(argc, argv, &startup, startup_error, sizeof(startup_error))) {
        fprintf(stderr, "%s\n%s", startup_error, viewbbc_startup_usage());
        return 2;
    }
    if (startup.show_help) {
        fputs(viewbbc_startup_usage(), stdout);
        return 0;
    }
    if (startup.show_mountlist) {
        ViewBBCCommandOutput output;
        char list_error[256];
        if (!viewbbc_startup_mountlist(&startup, &output, list_error, sizeof(list_error))) {
            fprintf(stderr, "%s\n", list_error[0] ? list_error : "Cannot list DFS image");
            return 1;
        }
        for (size_t i = 0; i < output.line_count; ++i) puts(output.lines[i]);
        return 0;
    }

    if (!viewbbc_config_load_or_create(&config)) {
        fprintf(stderr, "Failed to load/create viewbeeb.conf.\n");
        return 1;
    }

    if (!viewbbc_editor_init(&editor, VIEWBBC_DEFAULT_COLS, VIEWBBC_DEFAULT_ROWS)) {
        fprintf(stderr, "Failed to initialise ViewBBC core.\n");
        return 1;
    }

    viewbbc_editor_set_line_numbers(&editor, config.line_numbers);
    viewbbc_editor_set_rowcols(&editor, config.rowcols);
    (void)snprintf(editor.config_path, sizeof(editor.config_path), "%s", config.path);

    if (!viewbbc_document_set_workspace_limit(&editor.document, config.buffer_size)) {
        fprintf(stderr, "Configured buffer size is invalid for current document.\n");
        viewbbc_editor_destroy(&editor);
        return 1;
    }
    /* editor_init() renders once using the default 1 MiB workspace.
       Refresh immediately after applying the persisted configuration so the
       first screen's Bytes free value reflects the configured buffer size. */
    viewbbc_editor_render(&editor);

    (void)viewbbc_startup_apply(&editor, &startup);

    if (!viewbbc_sdl3_init(&config)) {
        fprintf(stderr, "Failed to initialise SDL3.\n");
        viewbbc_editor_destroy(&editor);
        return 1;
    }

    {
        int cols = 0, rows = 0;
        viewbbc_sdl3_get_grid_size(&cols, &rows);
        if ((cols != editor.screen.cols || rows != editor.screen.rows) &&
            !viewbbc_editor_resize(&editor, cols, rows)) {
            fprintf(stderr, "Failed to resize BeebView display.\n");
            viewbbc_sdl3_shutdown();
            viewbbc_editor_destroy(&editor);
            return 1;
        }
    }

    while (editor.running) {
        ViewBBCKey key;
        uint32_t ch;
        int is_text;
        ViewBBCSDLMouseEvent mouse;
        ViewBBCSDLClipboardAction clipboard_action;

        viewbbc_sdl3_render(&editor.screen);
        if (!viewbbc_sdl3_wait_input(&key, &ch, &is_text, &mouse, &clipboard_action)) break;

        {
            int cols = 0, rows = 0;
            viewbbc_sdl3_get_grid_size(&cols, &rows);
            if (cols != editor.screen.cols || rows != editor.screen.rows) {
                if (!viewbbc_editor_resize(&editor, cols, rows)) {
                    snprintf(editor.status_message, sizeof(editor.status_message),
                             "Display resize failed");
                    viewbbc_editor_render(&editor);
                }
            }
        }

        if (clipboard_action == VIEWBBC_SDL_CLIPBOARD_COPY) {
            copy_selection(&editor, 0);
            continue;
        }
        if (clipboard_action == VIEWBBC_SDL_CLIPBOARD_PASTE) {
            paste_clipboard(&editor, 0);
            continue;
        }
        if (clipboard_action == VIEWBBC_SDL_CLIPBOARD_CUT) {
            cut_selection(&editor);
            continue;
        }
        if (clipboard_action == VIEWBBC_SDL_CLIPBOARD_SELECT_ALL) {
            if (editor.mode == VIEWBBC_MODE_COMMAND) {
                viewbbc_editor_command_select_all(&editor);
                viewbbc_editor_render(&editor);
            } else {
                viewbbc_editor_select_all(&editor);
            }
            continue;
        }

        switch (mouse.type) {
            case VIEWBBC_SDL_MOUSE_DOWN: {
                if (editor.mode == VIEWBBC_MODE_COMMAND && mouse.button == 1) {
                    if (mouse.cell_y == command_prompt_row(&editor)) {
                        command_view_clear_selection(&editor);
                        viewbbc_editor_command_mouse_down(&editor, mouse.cell_x, mouse.cell_y);
                    } else {
                        command_view_mouse_down(&editor, mouse.cell_x, mouse.cell_y);
                    }
                    continue;
                }
                ViewBBCMouseAction action = (mouse.button >= 1 && mouse.button <= 6 && config.mouse_button_modifiers[mouse.button] == mouse.modifiers) ? config.mouse_buttons[mouse.button] : VIEWBBC_MOUSE_NONE;
                /* Linux/X11 convention: an unassigned middle click positions the
                   caret and pastes the PRIMARY selection. */
                if (mouse.button == 2 && action == VIEWBBC_MOUSE_NONE && editor.mode == VIEWBBC_MODE_TEXT) {
                    viewbbc_editor_mouse_click(&editor, mouse.cell_x, mouse.cell_y);
                    paste_clipboard(&editor, 1);
                }
                else if (action == VIEWBBC_MOUSE_SELECT ||
                         (mouse.button == 1 && config.mouse_buttons[1] == VIEWBBC_MOUSE_SELECT &&
                          config.mouse_button_modifiers[1] == 0 && mouse.modifiers == VIEWBBC_MOD_SHIFT)) {
                    if (mouse.clicks >= 3)
                        viewbbc_editor_mouse_select_line(&editor, mouse.cell_x, mouse.cell_y);
                    else if (mouse.clicks == 2)
                        viewbbc_editor_mouse_select_word(&editor, mouse.cell_x, mouse.cell_y);
                    else if (mouse.modifiers & VIEWBBC_MOD_SHIFT)
                        viewbbc_editor_mouse_shift_click(&editor, mouse.cell_x, mouse.cell_y);
                    else
                        viewbbc_editor_mouse_drag_begin(&editor, mouse.cell_x, mouse.cell_y);
                }
                else if (action == VIEWBBC_MOUSE_POSITION) viewbbc_editor_mouse_click(&editor, mouse.cell_x, mouse.cell_y);
                else if (action == VIEWBBC_MOUSE_COPY) viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_COPY);
                else if (action == VIEWBBC_MOUSE_MOVE) viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F0);
                else if (action == VIEWBBC_MOUSE_DELETE) viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F0);
                else if (action == VIEWBBC_MOUSE_FORMAT) viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_F0);
                continue;
            }
            case VIEWBBC_SDL_MOUSE_DRAG:
                if (editor.command_mouse_dragging) {
                    viewbbc_editor_command_mouse_drag(&editor, mouse.cell_x, mouse.cell_y);
                } else if (editor.command_view_dragging) {
                    command_view_mouse_drag(&editor, mouse.cell_x, mouse.cell_y);
                } else if (editor.mouse_dragging) {
                    if (mouse.cell_y < 1)
                        viewbbc_editor_mouse_drag_autoscroll(&editor, mouse.cell_x, -1);
                    else if (mouse.cell_y >= editor.screen.rows)
                        viewbbc_editor_mouse_drag_autoscroll(&editor, mouse.cell_x, 1);
                    else
                        viewbbc_editor_mouse_drag_update(&editor, mouse.cell_x, mouse.cell_y);
                }
                continue;
            case VIEWBBC_SDL_MOUSE_UP:
                if (editor.command_mouse_dragging) {
                    viewbbc_editor_command_mouse_up(&editor, mouse.cell_x, mouse.cell_y);
                } else if (editor.command_view_dragging) {
                    command_view_mouse_up(&editor, mouse.cell_x, mouse.cell_y);
                    if (editor.command_view_selection_active) copy_selection(&editor, 1);
                } else if (editor.mouse_dragging) {
                    viewbbc_editor_mouse_drag_end(&editor, mouse.cell_x, mouse.cell_y);
                    if (editor.mouse_selection_active) copy_selection(&editor, 1);
                }
                continue;
            case VIEWBBC_SDL_MOUSE_WHEEL:
                viewbbc_editor_mouse_wheel(&editor, mouse.wheel_steps * config.mouse_wheel_lines);
                continue;
            case VIEWBBC_SDL_MOUSE_NONE:
                break;
        }

        if (is_text) {
            if (editor.command_view_selection_active) command_view_clear_selection(&editor);
            if (editor.mouse_selection_active) delete_selection(&editor);
            viewbbc_editor_insert_char(&editor, ch);
        } else if (key != VIEWBBC_KEY_NONE) {
            if (editor.command_view_selection_active) command_view_clear_selection(&editor);
            if ((key == VIEWBBC_KEY_DELETE || key == VIEWBBC_KEY_BACKSPACE) &&
                editor.mode == VIEWBBC_MODE_TEXT && editor.mouse_selection_active)
                delete_selection(&editor);
            else
                viewbbc_editor_handle_key(&editor, key);
        }

        if (editor.buffer_size_changed) {
            editor.buffer_size_changed = 0;
            config.buffer_size = editor.buffer_size_requested;
            if (!viewbbc_config_save(&config))
                snprintf(editor.status_message, sizeof(editor.status_message), "Buffer changed, but configuration could not be saved");
            viewbbc_editor_render(&editor);
        }
        if (editor.line_numbers_changed) {
            editor.line_numbers_changed = 0;
            config.line_numbers = editor.line_numbers_requested;
            if (!viewbbc_config_save(&config))
                snprintf(editor.status_message, sizeof(editor.status_message), "Line numbers changed, but configuration could not be saved");
            viewbbc_editor_render(&editor);
        }
        if (editor.rowcols_changed) {
            editor.rowcols_changed = 0;
            config.rowcols = editor.rowcols_requested;
            if (!viewbbc_config_save(&config))
                snprintf(editor.status_message, sizeof(editor.status_message), "Row/column display changed, but configuration could not be saved");
            viewbbc_editor_render(&editor);
        }

        handle_print_request(&editor, &config);

        if (editor.config_reset_requested) {
            editor.config_reset_requested = 0;
            if (viewbbc_config_reset(&config)) {
                viewbbc_sdl3_apply_config(&config);
                (void)viewbbc_document_set_workspace_limit(&editor.document, config.buffer_size);
                viewbbc_editor_set_line_numbers(&editor, config.line_numbers);
                viewbbc_editor_set_rowcols(&editor, config.rowcols);
                snprintf(editor.status_message, sizeof(editor.status_message), "Configuration reset to defaults");
            } else {
                snprintf(editor.status_message, sizeof(editor.status_message), "Could not reset configuration");
            }
            viewbbc_editor_render(&editor);
        }
        if (editor.config_mode_requested) {
            editor.config_mode_requested = 0;
            (void)viewbbc_sdl3_run_config(&config);
            viewbbc_editor_set_line_numbers(&editor, config.line_numbers);
            viewbbc_editor_set_rowcols(&editor, config.rowcols);
            viewbbc_editor_render(&editor);
        }
    }

    viewbbc_sdl3_shutdown();
    viewbbc_editor_destroy(&editor);
    return 0;
}
