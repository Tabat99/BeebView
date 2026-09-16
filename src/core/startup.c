#include "viewbbc/startup.h"

#include "viewbbc/editor_commands.h"
#include "viewbbc/filesystem.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ascii_prefix_ieq(const char *text, const char *prefix) {
    if (!text || !prefix) return 0;
    while (*prefix) {
        if (*text == '\0') return 0;
        if (toupper((unsigned char)*text) != toupper((unsigned char)*prefix)) return 0;
        ++text; ++prefix;
    }
    return 1;
}

static void set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) return;
    (void)snprintf(error, error_size, "%s", message ? message : "");
}

static int decode_cli_value(const char *value, char *out, size_t out_size) {
    int has_argument = 0;
    if (!value || !*value) return 0;
    if (!viewbbc_command_decode_argument(value, out, out_size, &has_argument)) return 0;
    return has_argument;
}

static int parse_image_value(const char *value, ViewBBCDFSType *type,
                             char *path, size_t path_size) {
    if (!value || !type || !path || path_size == 0) return 0;
    *type = VIEWBBC_DFS_TYPE_UNKNOWN;
    if (ascii_prefix_ieq(value, "ssd:")) { *type = VIEWBBC_DFS_TYPE_SSD; value += 4; }
    else if (ascii_prefix_ieq(value, "dsd:")) { *type = VIEWBBC_DFS_TYPE_DSD; value += 4; }
    return decode_cli_value(value, path, path_size);
}

int viewbbc_startup_parse(int argc, char *argv[], ViewBBCStartupOptions *options,
                          char *error, size_t error_size) {
    if (!options || argc < 0 || (argc > 0 && !argv)) return 0;
    memset(options, 0, sizeof(*options));
    set_error(error, error_size, "");

    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (!arg || !*arg) { set_error(error, error_size, "Empty command-line argument"); return 0; }

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            if (argc != 2) { set_error(error, error_size, "--help cannot be combined with other arguments"); return 0; }
            options->show_help = 1; return 1;
        }

        if (ascii_prefix_ieq(arg, "--mountlist:")) {
            if (argc != 2) { set_error(error, error_size, "--mountlist cannot be combined with other arguments"); return 0; }
            if (!parse_image_value(arg + 12, &options->mount_type, options->mount_path, sizeof(options->mount_path))) {
                set_error(error, error_size, "--mountlist requires a disc-image filename"); return 0;
            }
            options->show_mountlist = 1; return 1;
        }

        if (ascii_prefix_ieq(arg, "mount:")) {
            if (options->has_mount) { set_error(error, error_size, "Only one mount: argument may be supplied"); return 0; }
            if (options->has_file) { set_error(error, error_size, "mount: must appear before the file to load"); return 0; }
            if (!parse_image_value(arg + 6, &options->mount_type, options->mount_path, sizeof(options->mount_path))) {
                set_error(error, error_size, "mount: requires a disc-image filename"); return 0;
            }
            options->has_mount = 1; continue;
        }

        if (options->has_file) { set_error(error, error_size, "Too many command-line filenames"); return 0; }
        if (!decode_cli_value(arg, options->file_path, sizeof(options->file_path))) {
            set_error(error, error_size, "Invalid or overlong startup filename"); return 0;
        }
        options->has_file = 1;
    }
    return 1;
}

static int put_quoted_command(ViewBBCEditor *editor, const char *command, const char *argument) {
    if (!editor || !command || !argument) return 0;
    size_t used = 0, command_len = strlen(command);
    if (command_len + 4u > sizeof(editor->command_buffer)) return 0;
    memcpy(editor->command_buffer, command, command_len); used = command_len;
    editor->command_buffer[used++] = ' '; editor->command_buffer[used++] = '"';
    for (const char *p = argument; *p; ++p) {
        if (*p == '"') {
            if (used + 2u >= sizeof(editor->command_buffer)) return 0;
            editor->command_buffer[used++] = '"'; editor->command_buffer[used++] = '"';
        } else {
            if (used + 1u >= sizeof(editor->command_buffer)) return 0;
            editor->command_buffer[used++] = *p;
        }
    }
    if (used + 2u > sizeof(editor->command_buffer)) return 0;
    editor->command_buffer[used++] = '"'; editor->command_buffer[used] = '\0';
    editor->command_length = used; editor->command_cursor = used;
    viewbbc_editor_execute_command(editor);
    editor->command_buffer[0] = '\0'; editor->command_length = 0; editor->command_cursor = 0;
    return 1;
}

int viewbbc_startup_apply(ViewBBCEditor *editor, const ViewBBCStartupOptions *options) {
    if (!editor || !options) return 0;
    if (options->has_mount) {
        char command[16] = "MOUNT";
        if (options->mount_type == VIEWBBC_DFS_TYPE_SSD) strcpy(command, "MOUNT SSD");
        else if (options->mount_type == VIEWBBC_DFS_TYPE_DSD) strcpy(command, "MOUNT DSD");
        if (!put_quoted_command(editor, command, options->mount_path)) {
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Startup mount argument is too long"); return 0;
        }
        if (!editor->image_mounted) return 0;
    }
    if (options->has_file) {
        if (!put_quoted_command(editor, "LOAD", options->file_path)) {
            (void)snprintf(editor->status_message, sizeof(editor->status_message), "Startup filename is too long"); return 0;
        }
        if (!editor->current_file) return 0;
        editor->mode = VIEWBBC_MODE_TEXT;
        viewbbc_editor_render(editor);
    }
    return 1;
}

int viewbbc_startup_mountlist(const ViewBBCStartupOptions *options,
                              ViewBBCCommandOutput *output,
                              char *error, size_t error_size) {
    if (!options || !output || !options->show_mountlist) return 0;
    viewbbc_output_clear(output); set_error(error, error_size, "");

    ViewBBCDFSType type = options->mount_type;
    if (type == VIEWBBC_DFS_TYPE_UNKNOWN) type = viewbbc_dfs_type_from_path(options->mount_path);
    if (type == VIEWBBC_DFS_TYPE_UNKNOWN) { set_error(error, error_size, "Unknown disc type; use ssd: or dsd:"); return 0; }

    ViewBBCFilesystemState fs;
    if (!viewbbc_fs_init(&fs)) { set_error(error, error_size, "Cannot determine current directory"); return 0; }
    char *path = NULL;
    int resolved = viewbbc_fs_resolve_native(&fs, options->mount_path, &path);
    viewbbc_fs_destroy(&fs);
    if (!resolved) { set_error(error, error_size, "Invalid or overlong image path"); return 0; }

    ViewBBCDFSImage image;
    if (!viewbbc_dfs_open_as(&image, path, type)) {
        free(path); set_error(error, error_size, "Cannot open or parse DFS image"); return 0;
    }
    free(path);

    for (int side = 0; side < image.side_count; ++side) {
        int drive = side == 0 ? 0 : 2;
        for (size_t i = 0; i < image.sides[side].file_count; ++i) {
            const ViewBBCDFSFile *file = &image.sides[side].files[i];
            (void)viewbbc_output_addf(output, ":%d.%s", drive, file->name);
        }
    }
    viewbbc_dfs_close(&image);
    return 1;
}

const char *viewbbc_startup_usage(void) {
    return "Usage: viewbbc [mount:[ssd:|dsd:]<disc-image>] [file]\n"
           "       viewbbc --mountlist:[ssd:|dsd:]<disc-image>\n"
           "       viewbbc --help\n"
           "\n"
           "A successfully loaded startup file opens directly in TEXT mode.\n"
           "The disc type is inferred from .ssd/.dsd when omitted.\n"
           "--mountlist lists every DFS file and exits.\n"
           "Examples:\n"
           "  viewbbc mount:\"My Disc.ssd\" \"LETTER\"\n"
           "  viewbbc mount:dsd:\"My Disc.img\" \":2.REPORT\"\n"
           "  viewbbc --mountlist:ssd:\"My Disc.img\"\n"
           "  viewbbc \"notes/view file\"\n";
}
