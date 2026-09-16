#include <stdio.h>
#include <string.h>

#include "viewbbc/editor.h"
#include "viewbbc/config.h"
#include "viewbbc/print.h"
#include "viewbbc/startup.h"
#include "frontend/ncurses/ncurses_frontend.h"
#include "frontend/ncurses/terminal_launch.h"

int main(int argc, char *argv[]) {
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

    if (!viewbbc_terminal_prepare(argc, argv)) {
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

    if (!viewbbc_ncurses_init(&config)) {
        fprintf(stderr, "Failed to initialise ncurses.\n");
        viewbbc_editor_destroy(&editor);
        return 1;
    }

    while (editor.running) {
        ViewBBCKey key;
        uint32_t ch;
        int is_text;

        viewbbc_ncurses_render(&editor.screen);
        if (!viewbbc_ncurses_read_input(&key, &ch, &is_text)) {
            continue;
        }

        if (is_text) {
            viewbbc_editor_insert_char(&editor, ch);
        } else {
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

        if (editor.print_requested) {
            editor.print_requested = 0;
            const char *target = editor.print_target_override[0]
                ? editor.print_target_override : config.print_target;
            char chosen[VIEWBBC_CONFIG_VALUE_MAX + 32u];
            if (strcmp(target, "file") == 0) {
                if (!viewbbc_ncurses_choose_print_file(chosen, sizeof(chosen))) {
                    snprintf(editor.status_message, sizeof(editor.status_message), "Print cancelled");
                    editor.print_target_override[0] = '\0';
                    viewbbc_editor_render(&editor);
                    continue;
                }
                target = chosen;
            }
            (void)viewbbc_print_document(&editor.document, target,
                                         editor.status_message, sizeof(editor.status_message));
            editor.print_target_override[0] = '\0';
            viewbbc_editor_render(&editor);
        }
        if (editor.config_reset_requested) {
            editor.config_reset_requested = 0;
            if (viewbbc_config_reset(&config)) {
                (void)viewbbc_document_set_workspace_limit(&editor.document, config.buffer_size);
                viewbbc_editor_set_line_numbers(&editor, config.line_numbers);
                viewbbc_editor_set_rowcols(&editor, config.rowcols);
                snprintf(editor.status_message, sizeof(editor.status_message), "Configuration reset to defaults");
            }
            else
                snprintf(editor.status_message, sizeof(editor.status_message), "Could not reset configuration");
            viewbbc_editor_render(&editor);
        }
        if (editor.config_mode_requested) {
            editor.config_mode_requested = 0;
            snprintf(editor.status_message, sizeof(editor.status_message), "CONFIG editor is available in the SDL frontend");
            viewbbc_editor_render(&editor);
        }
    }

    viewbbc_ncurses_shutdown();
    viewbbc_editor_destroy(&editor);
    return 0;
}
