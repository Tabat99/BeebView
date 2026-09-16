#include "viewbbc/editor_commands.h"

#include "viewbbc/commands.h"
#include "viewbbc/dfs.h"
#include "viewbbc/file_io.h"
#include "viewbbc/filesystem.h"
#include "viewbbc/help.h"
#include "viewbbc/output.h"
#include "viewbbc/version.h"
#include "viewbbc/view_commands.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static char *duplicate_string(const char *text) {
    if (!text) return NULL;
    size_t length = strlen(text);
    if (length >= VIEWBBC_PATH_LIMIT || length == SIZE_MAX) return NULL;
    char *copy = malloc(length + 1u);
    if (!copy) return NULL;
    memcpy(copy, text, length + 1u);
    return copy;
}

static int ascii_ieq(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static void set_status(ViewBBCEditor *editor, const char *message) {
    if (!editor) return;
    if (!message) message = "";
    (void)snprintf(editor->status_message, sizeof(editor->status_message), "%s", message);
}

static int replace_string(char **target, const char *value) {
    if (!target) return 0;
    char *replacement = value ? duplicate_string(value) : NULL;
    if (value && !replacement) return 0;
    free(*target);
    *target = replacement;
    return 1;
}

static void reset_edit_position(ViewBBCEditor *editor) {
    editor->cursor_line = 0;
    editor->cursor_column = 0;
    editor->viewport_top = 0;
    editor->viewport_left = 0;
}

static int set_current_file(ViewBBCEditor *editor, const char *name, int read_only) {
    if (!replace_string(&editor->current_file, name)) return 0;
    editor->current_source_read_only = read_only;
    return 1;
}

static void command_new_now(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (command->has_argument) {
        set_status(editor, "Bad NEW command");
        return;
    }
    viewbbc_document_clear(&editor->document);
    (void)set_current_file(editor, NULL, 0);
    reset_edit_position(editor);
    editor->file_modified = 0;
    set_status(editor, "New file");
}

static int parse_mount_arguments(const ViewBBCParsedCommand *command,
                                 ViewBBCDFSType *type_out,
                                 char *filename,
                                 size_t filename_size,
                                 int *has_filename) {
    if (!command || !type_out || !filename || filename_size == 0 || !has_filename) return 0;
    *type_out = VIEWBBC_DFS_TYPE_UNKNOWN;
    *has_filename = 0;
    filename[0] = '\0';
    if (!command->has_argument) return 1;

    const char *p = command->argument;
    while (*p && isspace((unsigned char)*p)) ++p;

    if (*p != '"') {
        const char *token = p;
        while (*p && !isspace((unsigned char)*p)) ++p;
        size_t token_len = (size_t)(p - token);
        if (token_len == 3) {
            char t[4] = { token[0], token[1], token[2], '\0' };
            if (ascii_ieq(t, "SSD")) *type_out = VIEWBBC_DFS_TYPE_SSD;
            else if (ascii_ieq(t, "DSD")) *type_out = VIEWBBC_DFS_TYPE_DSD;
        }
        if (*type_out != VIEWBBC_DFS_TYPE_UNKNOWN) {
            while (*p && isspace((unsigned char)*p)) ++p;
        } else {
            p = command->argument;
        }
    }

    return viewbbc_command_decode_argument(p, filename, filename_size, has_filename);
}

static void command_mount_status(ViewBBCEditor *editor) {
    viewbbc_output_clear(&editor->command_output);
    if (!editor->image_mounted) {
        set_status(editor, "No disc mounted");
        return;
    }
    set_status(editor, "Disc mounted");
    (void)viewbbc_output_addf(&editor->command_output, "Image: %s",
                              editor->mounted_image_path ? editor->mounted_image_path : "");
    (void)viewbbc_output_addf(&editor->command_output, "Type: %s",
                              viewbbc_dfs_type_name(editor->mounted_image.type));
    (void)viewbbc_output_addf(&editor->command_output, "Directory: %c", editor->filesystem.dfs_directory);
}

static void command_mount(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        command_mount_status(editor);
        return;
    }

    ViewBBCDFSType type;
    char filename[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    int has_filename;
    if (!parse_mount_arguments(command, &type, filename, sizeof(filename), &has_filename)) {
        set_status(editor, "MOUNT argument is too long");
        return;
    }
    if (!has_filename) {
        set_status(editor, "MOUNT needs a filename");
        return;
    }
    if (type == VIEWBBC_DFS_TYPE_UNKNOWN) type = viewbbc_dfs_type_from_path(filename);
    if (type == VIEWBBC_DFS_TYPE_UNKNOWN) {
        set_status(editor, "Unknown or missing disc type");
        return;
    }

    char *path = NULL;
    if (!viewbbc_fs_resolve_native(&editor->filesystem, filename, &path)) {
        set_status(editor, "Invalid or overlong image path");
        return;
    }

    struct stat st;
    if (stat(path, &st) != 0) {
        viewbbc_output_clear(&editor->command_output);
        (void)viewbbc_output_addf(&editor->command_output, "Path: %s", path);
        free(path);
        set_status(editor, "Disc image not found");
        return;
    }
    if (!S_ISREG(st.st_mode)) {
        viewbbc_output_clear(&editor->command_output);
        (void)viewbbc_output_addf(&editor->command_output, "Path: %s", path);
        free(path);
        set_status(editor, "Disc image is not a regular file");
        return;
    }
    if (st.st_size < 2 * 256) {
        viewbbc_output_clear(&editor->command_output);
        (void)viewbbc_output_addf(&editor->command_output, "Path: %s", path);
        free(path);
        set_status(editor, "DFS image is too small");
        return;
    }

    ViewBBCDFSImage image;
    if (!viewbbc_dfs_open_as(&image, path, type)) {
        viewbbc_output_clear(&editor->command_output);
        (void)viewbbc_output_addf(&editor->command_output, "Path: %s", path);
        (void)viewbbc_output_addf(&editor->command_output, "Type: %s", viewbbc_dfs_type_name(type));
        free(path);
        set_status(editor, "Invalid or unsupported DFS image");
        return;
    }

    char *path_copy = duplicate_string(path);
    free(path);
    if (!path_copy) {
        viewbbc_dfs_close(&image);
        set_status(editor, "No memory");
        return;
    }

    if (editor->image_mounted) viewbbc_dfs_close(&editor->mounted_image);
    free(editor->mounted_image_path);
    editor->mounted_image = image;
    editor->mounted_image_path = path_copy;
    editor->image_mounted = 1;
    editor->filesystem.dfs_directory = '$';
    set_status(editor, "DFS image mounted read only");
}

static void command_unmount(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (command->has_argument) {
        set_status(editor, "UNMOUNT takes no parameters");
        return;
    }
    if (!editor->image_mounted) {
        set_status(editor, "No disc mounted");
        return;
    }
    viewbbc_dfs_close(&editor->mounted_image);
    free(editor->mounted_image_path);
    editor->mounted_image_path = NULL;
    editor->image_mounted = 0;
    editor->filesystem.dfs_directory = '$';
    set_status(editor, "Disc unmounted - native filesystem active");
}

static int dfs_qualify_name(const ViewBBCEditor *editor,
                            const char *input,
                            char *out,
                            size_t out_size) {
    if (!editor || !input || !*input || !out || out_size == 0) return 0;

    const char *name = input;
    char drive_prefix[4] = "";
    if (name[0] == ':' && name[1] && name[2] == '.') {
        drive_prefix[0] = ':';
        drive_prefix[1] = name[1];
        drive_prefix[2] = '.';
        drive_prefix[3] = '\0';
        name += 3;
    }

    if (strchr(name, '.')) {
        int n = snprintf(out, out_size, "%s%s", drive_prefix, name);
        return n >= 0 && (size_t)n < out_size;
    }

    int n = snprintf(out, out_size, "%s%c.%s", drive_prefix, editor->filesystem.dfs_directory, name);
    return n >= 0 && (size_t)n < out_size;
}

static void command_load_from_dfs(ViewBBCEditor *editor, const char *name) {
    char qualified[64];
    if (!dfs_qualify_name(editor, name, qualified, sizeof(qualified))) {
        set_status(editor, "DFS filename is too long");
        return;
    }

    const ViewBBCDFSFile *file = viewbbc_dfs_find_file(&editor->mounted_image, qualified);
    if (!file) {
        set_status(editor, "File not found on mounted disc");
        return;
    }
    if ((size_t)file->length > viewbbc_document_workspace_limit(&editor->document)) {
        set_status(editor, "File is larger than workspace");
        return;
    }

    uint8_t *data = NULL;
    size_t length = 0;
    if (!viewbbc_dfs_extract_file(&editor->mounted_image, file, &data, &length)) {
        set_status(editor, "Cannot extract DFS file");
        return;
    }
    int ok = viewbbc_document_load_bytes(&editor->document, data, length);
    free(data);
    if (!ok) {
        set_status(editor, "File is too large or invalid for workspace");
        return;
    }

    char display[128];
    int drive = file->side == 0 ? 0 : 2;
    int written = snprintf(display, sizeof(display), ":%d.%s", drive, file->name);
    if (written < 0 || (size_t)written >= sizeof(display) || !set_current_file(editor, display, 1)) {
        set_status(editor, "Loaded, but cannot record filename");
    } else {
        set_status(editor, "Loaded from DFS image");
    }
    reset_edit_position(editor);
    editor->file_modified = 0;
}

static void command_load_now(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        set_status(editor, "LOAD needs a filename");
        return;
    }

    if (editor->image_mounted) {
        command_load_from_dfs(editor, command->argument);
        return;
    }

    char *path = NULL;
    if (!viewbbc_fs_resolve_native(&editor->filesystem, command->argument, &path)) {
        set_status(editor, "Invalid or overlong filename");
        return;
    }
    if (!viewbbc_file_load_document(path, &editor->document)) {
        free(path);
        set_status(editor, "Cannot load file or file exceeds workspace");
        return;
    }
    if (!set_current_file(editor, path, 0)) set_status(editor, "Loaded, but cannot record filename");
    else set_status(editor, "Loaded");
    free(path);
    reset_edit_position(editor);
    editor->file_modified = 0;
}

static void command_save(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (editor->image_mounted) {
        set_status(editor, "Mounted DFS image is read only");
        return;
    }

    char *resolved = NULL;
    const char *path = NULL;
    if (command->has_argument) {
        if (!viewbbc_fs_resolve_native(&editor->filesystem, command->argument, &resolved)) {
            set_status(editor, "Invalid or overlong filename");
            return;
        }
        path = resolved;
    } else if (editor->current_file && !editor->current_source_read_only) {
        path = editor->current_file;
    } else if (editor->current_source_read_only) {
        set_status(editor, "Read only source - use SAVE filename after UNMOUNT");
        return;
    } else {
        set_status(editor, "SAVE needs a filename");
        return;
    }

    if (!viewbbc_file_save_document(path, &editor->document)) {
        free(resolved);
        set_status(editor, "Cannot save file");
        return;
    }
    if (!set_current_file(editor, path, 0)) set_status(editor, "Saved, but cannot record filename");
    else set_status(editor, "Saved");
    editor->file_modified = 0;
    free(resolved);
}

static int begin_confirmation(ViewBBCEditor *editor, ViewBBCConfirmAction action,
                              const char *argument, const char *message) {
    if (!editor || !message) return 0;
    editor->confirm_argument[0] = '\0';
    if (argument) {
        size_t length = strlen(argument);
        if (length >= sizeof(editor->confirm_argument)) {
            set_status(editor, "Filename is too long");
            return 0;
        }
        memcpy(editor->confirm_argument, argument, length + 1u);
    }
    editor->confirm_action = action;
    editor->mode = VIEWBBC_MODE_COMMAND;
    set_status(editor, message);
    return 1;
}

static void command_load(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (editor->file_modified) {
        if (!command->has_argument) {
            set_status(editor, "LOAD needs a filename");
            return;
        }
        (void)begin_confirmation(editor, VIEWBBC_CONFIRM_LOAD, command->argument,
                                 "File modified - load another file? (Y/N)");
        return;
    }
    command_load_now(editor, command);
}

static void command_new(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (editor->file_modified && !command->has_argument) {
        (void)begin_confirmation(editor, VIEWBBC_CONFIRM_NEW, NULL,
                                 "File modified - start a new file? (Y/N)");
        return;
    }
    command_new_now(editor, command);
}

void viewbbc_editor_request_exit(ViewBBCEditor *editor) {
    if (!editor) return;
    if (!editor->file_modified) {
        editor->running = 0;
        return;
    }
    (void)begin_confirmation(editor, VIEWBBC_CONFIRM_EXIT, NULL,
                             "File modified - exit without saving? (Y/N)");
}

void viewbbc_editor_handle_confirmation(ViewBBCEditor *editor, int yes) {
    if (!editor || editor->confirm_action == VIEWBBC_CONFIRM_NONE) return;

    ViewBBCConfirmAction action = editor->confirm_action;
    char argument[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    (void)snprintf(argument, sizeof(argument), "%s", editor->confirm_argument);
    editor->confirm_action = VIEWBBC_CONFIRM_NONE;
    editor->confirm_argument[0] = '\0';

    if (!yes) {
        if (action == VIEWBBC_CONFIRM_EXIT) {
            (void)viewbbc_console_add(&editor->console,
                                      "File modified - exit without saving? (Y/N) No");
            set_status(editor, "");
            return;
        }
        set_status(editor, "Cancelled");
        (void)viewbbc_console_add(&editor->console, "Cancelled");
        return;
    }

    if (action == VIEWBBC_CONFIRM_EXIT) {
        editor->running = 0;
        return;
    }
    if (action == VIEWBBC_CONFIRM_CONFIG_RESET) {
        editor->config_reset_requested = 1;
        set_status(editor, "Resetting configuration");
        return;
    }

    ViewBBCParsedCommand command = {0};
    if (action == VIEWBBC_CONFIRM_LOAD) {
        command.type = VIEWBBC_COMMAND_LOAD;
        command.has_argument = argument[0] != '\0';
        (void)snprintf(command.argument, sizeof(command.argument), "%s", argument);
        viewbbc_output_clear(&editor->command_output);
        set_status(editor, "");
        command_load_now(editor, &command);
    } else if (action == VIEWBBC_CONFIRM_NEW) {
        command.type = VIEWBBC_COMMAND_NEW;
        command_new_now(editor, &command);
    }

    if (editor->status_message[0]) (void)viewbbc_console_add(&editor->console, editor->status_message);
}

static int dfs_option_flags(ViewBBCCommandType type, const char *options,
                            int *long_form, int *bare, int *wide) {
    *long_form = 0;
    *bare = 0;
    *wide = 0;
    if (!options || !*options) return 1;
    if (type == VIEWBBC_COMMAND_LIST) return 0;

    size_t n = strlen(options);
    if (n > VIEWBBC_COMMAND_ARGUMENT_MAX) return 0;
    char buffer[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    memcpy(buffer, options, n + 1u);
    char *p = buffer;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) ++p;
        if (!*p) break;
        char *token = p;
        while (*p && !isspace((unsigned char)*p)) ++p;
        if (*p) *p++ = '\0';

        if (type == VIEWBBC_COMMAND_LS) {
            if (token[0] != '-') return 0;
            for (size_t i = 1; token[i]; ++i) {
                if (token[i] == 'l') *long_form = 1;
                else if (token[i] == 'a') { }
                else return 0;
            }
        } else {
            if (token[0] != '/' && token[0] != '-') return 0;
            for (size_t i = 1; token[i]; ++i) {
                int ch = toupper((unsigned char)token[i]);
                if (ch == 'B') *bare = 1;
                else if (ch == 'W') *wide = 1;
                else if (ch == 'A') { }
                else return 0;
            }
        }
    }
    return 1;
}

static void output_dfs_wide(ViewBBCCommandOutput *output,
                            const ViewBBCDFSFile *const *files,
                            size_t count) {
    const size_t columns = 5u;
    const size_t column_width = 15u;
    char line[VIEWBBC_OUTPUT_LINE_MAX + 1];

    for (size_t first = 0; first < count; first += columns) {
        size_t used = 0;
        line[used++] = ' ';
        line[used] = '\0';

        for (size_t col = 0; col < columns && first + col < count; ++col) {
            const char *display = files[first + col]->name + 2;
            size_t n = strlen(display);
            if (n > sizeof(line) - used - 1u) {
                output->truncated = 1;
                break;
            }
            memcpy(line + used, display, n);
            used += n;

            if (first + col + 1u < count && col + 1u < columns) {
                size_t padding = column_width > n ? column_width - n : 1u;
                if (padding > sizeof(line) - used - 1u) {
                    output->truncated = 1;
                    break;
                }
                memset(line + used, ' ', padding);
                used += padding;
            }
            line[used] = '\0';
        }
        while (used > 1u && line[used - 1u] == ' ') line[--used] = '\0';
        (void)viewbbc_output_add(output, line);
    }
}

static void list_dfs(ViewBBCEditor *editor, ViewBBCCommandType type, const char *options) {
    int long_form, bare, wide;
    if (!dfs_option_flags(type, options, &long_form, &bare, &wide)) {
        set_status(editor, type == VIEWBBC_COMMAND_DIR ? "Unsupported DIR option" :
                           type == VIEWBBC_COMMAND_LS ? "Unsupported LS option" : "LIST takes no parameters");
        return;
    }

    viewbbc_output_clear(&editor->command_output);
    if (!bare) {
        (void)viewbbc_output_addf(&editor->command_output, "%s  %s  directory %c",
                                  editor->mounted_image_path ? editor->mounted_image_path : "DFS image",
                                  viewbbc_dfs_type_name(editor->mounted_image.type),
                                  editor->filesystem.dfs_directory);
    }

    for (int side = 0; side < editor->mounted_image.side_count; ++side) {
        const ViewBBCDFSSide *disc_side = &editor->mounted_image.sides[side];
        if (!bare && editor->mounted_image.side_count > 1) {
            (void)viewbbc_output_addf(&editor->command_output, ":%d  %s", side == 0 ? 0 : 2, disc_side->title);
        }

        const ViewBBCDFSFile *matching[VIEWBBC_DFS_MAX_FILES_PER_SIDE];
        size_t matching_count = 0;
        for (size_t i = 0; i < disc_side->file_count; ++i) {
            const ViewBBCDFSFile *file = &disc_side->files[i];
            if (toupper((unsigned char)file->name[0]) !=
                toupper((unsigned char)editor->filesystem.dfs_directory)) continue;
            if (matching_count < VIEWBBC_DFS_MAX_FILES_PER_SIDE) {
                matching[matching_count++] = file;
            } else {
                editor->command_output.truncated = 1;
            }
        }

        if (wide && !bare && !long_form) {
            output_dfs_wide(&editor->command_output, matching, matching_count);
            continue;
        }

        for (size_t i = 0; i < matching_count; ++i) {
            const ViewBBCDFSFile *file = matching[i];
            const char *display = file->name + 2;
            if (long_form) {
                (void)viewbbc_output_addf(&editor->command_output, " %c %-7s %10u  %06X %06X",
                                          file->locked ? 'L' : ' ', display,
                                          (unsigned)file->length,
                                          (unsigned)file->load_address,
                                          (unsigned)file->exec_address);
            } else if (bare) {
                (void)viewbbc_output_add(&editor->command_output, display);
            } else {
                (void)viewbbc_output_addf(&editor->command_output, " %s", display);
            }
        }
    }
    set_status(editor, "Catalogue");
}

static void command_list(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    ViewBBCListStyle style = VIEWBBC_LIST_STYLE_VIEW;
    if (command->type == VIEWBBC_COMMAND_DIR) style = VIEWBBC_LIST_STYLE_DIR;
    else if (command->type == VIEWBBC_COMMAND_LS) style = VIEWBBC_LIST_STYLE_LS;

    if (editor->image_mounted) {
        list_dfs(editor, command->type, command->has_argument ? command->argument : "");
        return;
    }

    if (command->type == VIEWBBC_COMMAND_LIST && command->has_argument) {
        set_status(editor, "LIST takes no parameters");
        return;
    }

    char error[VIEWBBC_STATUS_MAX + 1];
    if (!viewbbc_fs_list_native(&editor->filesystem, style,
                                command->has_argument ? command->argument : "",
                                &editor->command_output, error, sizeof(error))) {
        set_status(editor, error);
        return;
    }
    set_status(editor, "Directory");
}

static void command_cd_dfs(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        set_status(editor, "Current DFS directory");
        (void)viewbbc_output_addf(&editor->command_output, "%c", editor->filesystem.dfs_directory);
        return;
    }

    const char *p = command->argument;
    size_t n = strlen(p);
    char dir;
    if ((n == 1 && p[0] == '.') ) dir = editor->filesystem.dfs_directory;
    else if ((n == 2 && p[0] == '.' && p[1] == '.') ||
             (n == 1 && (p[0] == '/' || p[0] == '\\' || p[0] == '$'))) dir = '$';
    else if ((n == 1 || (n == 2 && p[1] == '.')) && isalnum((unsigned char)p[0])) dir = (char)toupper((unsigned char)p[0]);
    else {
        set_status(editor, "Invalid DFS directory");
        return;
    }

    editor->filesystem.dfs_directory = dir;
    set_status(editor, "DFS directory changed");
    (void)viewbbc_output_addf(&editor->command_output, "%c", dir);
}

static void command_cd(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (editor->image_mounted) {
        command_cd_dfs(editor, command);
        return;
    }

    if (!command->has_argument) {
        set_status(editor, "Current folder");
        (void)viewbbc_output_add(&editor->command_output,
                                 editor->filesystem.native_cwd ? editor->filesystem.native_cwd : "");
        return;
    }

    char error[VIEWBBC_STATUS_MAX + 1];
    if (!viewbbc_fs_chdir_native(&editor->filesystem, command->argument, error, sizeof(error))) {
        set_status(editor, error);
        return;
    }
    set_status(editor, "Folder changed");
    (void)viewbbc_output_add(&editor->command_output, editor->filesystem.native_cwd);
}


static void add_key_help(ViewBBCEditor *editor) {
    static const struct { const char *key; const char *description; } keys[] = {
        { "ESC",       "Toggle COMMAND / TEXT mode" },
        { "Cmd Up/Down", "Browse command history" },
        { "Cmd Left/Right", "Edit within the command line" },
        { "Cmd Tab",     "Complete file/path; repeat to list matches" },
        { "Backspace", "VIEW black DELETE: blank character to left" },
        { "Delete/F9", "Delete character under cursor and close gap" },
        { "Insert",    "Toggle Insert Mode (alias for Ctrl-F4)" },
        { "Tab",       "Move to next ruler tab stop" },
        { "Home/F4",   "Beginning of current line" },
        { "End/F5",    "End of current line" },
        { "Page Up",   "Move one visible text page upward" },
        { "Page Down", "Move one visible text page downward" },
        { "Ctrl-Up",   "Same as Page Up" },
        { "Ctrl-Down", "Same as Page Down" },
        { "Return",    "Move to beginning of next line" },
        { "F1",        "Top of text" },
        { "F2",        "Bottom of text" },
        { "F3",        "Delete end of line" },
        { "F6",        "Insert line" },
        { "F7",        "Delete line" },
        { "F8",        "Insert character" },
        { "F10/F0",    "Format block from current paragraph line" },
        { "Ctrl-F0",   "Delete block between markers 1 and 2" },
        { "Ctrl-F1",   "Next match from the current SEARCH" },
        { "Ctrl-F2",   "Toggle Format Mode" },
        { "Ctrl-F3",   "Toggle Justify Mode" },
        { "Ctrl-F4",   "Toggle Insert Mode" },
        { "Ctrl-F5",   "Restore default ruler" },
        { "Ctrl-F6",   "Split line" },
        { "Ctrl-F7",   "Concatenate lines" },
        { "Ctrl-F8",   "Mark current line as ruler" },
        { "Shift-F8",  "Edit stored/embedded command" },
        { "Shift-F9",  "Delete stored/embedded command" },
        { "Shift-F0",  "Move block between markers 1 and 2" },
        { "Shift-F1",  "Swap case of character and move right" },
        { "Shift-F3",  "Delete up to next typed character" },
        { "F11",       "COPY key: copy block between markers 1 and 2" },
        { "Shift-F6",  "Go to marker 1-6" },
        { "Shift-F7",  "Set marker 1-6" },
        { "Mouse click", "Move text cursor (SDL frontend)" },
        { "Mouse drag",  "Select block into markers 1 and 2" },
        { "Mouse wheel", "Scroll text; line count is configurable" },
        { "Config",      "Key/mouse bindings are configurable in viewbeeb.conf" }
    };
    (void)viewbbc_output_add(&editor->command_output, "Keys:");
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i)
        (void)viewbbc_output_addf(&editor->command_output, " %-12s %s",
                                  keys[i].key, keys[i].description);
}

static void build_help_output(ViewBBCEditor *editor) {
    if (!editor) return;
    viewbbc_output_clear(&editor->command_output);
    (void)viewbbc_output_addf(&editor->command_output, "%s %s",
                              VIEWBBC_PRODUCT_NAME, VIEWBBC_VERSION_STRING);
    (void)viewbbc_output_add(&editor->command_output, "");

    if (editor->help_topic == VIEWBBC_HELP_TOPIC_ALL) {
        (void)viewbbc_output_add(&editor->command_output, "  HELP <subject>");
        (void)viewbbc_output_add(&editor->command_output, "    subjects:");
        (void)viewbbc_output_add(&editor->command_output, "      File");
        (void)viewbbc_output_add(&editor->command_output, "      Edit");
        (void)viewbbc_output_add(&editor->command_output, "      Keys");
        (void)viewbbc_output_add(&editor->command_output, "      Conf");
        return;
    }
    if (editor->help_topic == VIEWBBC_HELP_TOPIC_KEYS) {
        add_key_help(editor);
        return;
    }

    size_t count = 0;
    const ViewBBCHelpEntry *entries = viewbbc_help_entries(&count);
    for (size_t i = 0; i < count; ++i) {
        if (editor->help_topic == VIEWBBC_HELP_TOPIC_FILES &&
            entries[i].group != VIEWBBC_HELP_FILE_SYSTEM) continue;
        if (editor->help_topic == VIEWBBC_HELP_TOPIC_EDIT &&
            entries[i].group != VIEWBBC_HELP_EDITING) continue;
        if (editor->help_topic == VIEWBBC_HELP_TOPIC_CONF &&
            entries[i].group != VIEWBBC_HELP_CONFIGURATION) continue;
        (void)viewbbc_output_addf(&editor->command_output, " %-22s %s",
                                  entries[i].syntax, entries[i].description);
    }
}

static size_t output_page_capacity(const ViewBBCEditor *editor) {
    if (!editor || editor->screen.rows <= 3) return 1u;
    return (size_t)editor->screen.rows - 2u;
}

static void append_output_page(ViewBBCEditor *editor, int continuous) {
    if (!editor) return;
    size_t total = editor->command_output.line_count;
    size_t next = editor->output_page_next;
    if (next > total) next = total;

    size_t remaining = total - next;
    size_t capacity = continuous ? remaining : output_page_capacity(editor);
    int will_pause = !continuous && remaining > capacity;
    if (will_pause && capacity > 1u) capacity--;
    if (capacity > remaining) capacity = remaining;

    for (size_t i = 0; i < capacity; ++i)
        (void)viewbbc_console_add(&editor->console, editor->command_output.lines[next + i]);
    next += capacity;
    editor->output_page_next = next;

    if (next < total && !continuous) {
        (void)viewbbc_console_add(&editor->console,
                                  "-- More --  SPACE=continue  ESC=show all");
        editor->output_paging = 1;
        editor->output_paging_continuous = 0;
    } else {
        editor->output_paging = 0;
        editor->output_paging_continuous = continuous ? 1 : 0;
        if (editor->command_output.truncated)
            (void)viewbbc_console_add(&editor->console, "[output truncated]");
    }
}

void viewbbc_editor_continue_output(ViewBBCEditor *editor, int continuous) {
    if (!editor || !editor->output_paging) return;
    (void)viewbbc_console_remove_last(&editor->console); /* remove -- More -- */
    if (continuous) editor->output_paging_continuous = 1;
    append_output_page(editor, continuous || editor->output_paging_continuous);
}

static void command_setup(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!editor || !command) return;
    if (!command->has_argument) {
        (void)snprintf(editor->status_message, sizeof(editor->status_message),
                       "SETUP %s%s%s", editor->format_mode ? "F" : "",
                       editor->justify_mode ? "K" : "", editor->insert_mode ? "W" : "");
        return;
    }

    int format = 0, justify = 0, insert = 0;
    for (const char *p = command->argument; *p; ++p) {
        unsigned char ch = (unsigned char)*p;
        if (isspace(ch)) continue;
        switch (toupper(ch)) {
            case 'F': format = 1; break;
            case 'K': justify = 1; break;
            case 'J': justify = 1; break; /* BeebView 0.5.43-0.5.58 compatibility alias. */
            case 'W': insert = 1; break;
            default:
                set_status(editor, "SETUP uses F, K and W");
                return;
        }
    }
    editor->format_mode = format;
    editor->justify_mode = justify;
    editor->insert_mode = insert;
    (void)snprintf(editor->status_message, sizeof(editor->status_message),
                   "SETUP %s%s%s", format ? "F" : "", justify ? "K" : "", insert ? "W" : "");
}

static void command_buffersize(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!editor || !command) return;
    if (!command->has_argument) {
        char size_text[48];
        viewbbc_workspace_format_size(viewbbc_document_workspace_limit(&editor->document),
                                      size_text, sizeof(size_text));
        set_status(editor, size_text);
        (void)viewbbc_output_addf(&editor->command_output, "Buffer size: %s", size_text);
        return;
    }

    size_t bytes = 0;
    if (!viewbbc_workspace_parse_size(command->argument, &bytes)) {
        set_status(editor, "BUFFERSIZE needs bytes, KB or MB");
        return;
    }
    if (bytes < VIEWBBC_MIN_WORKSPACE_LIMIT) {
        set_status(editor, "Minimum buffer size is 32 KB");
        return;
    }
    if (bytes > VIEWBBC_MAX_WORKSPACE_LIMIT) {
        set_status(editor, "Maximum buffer size is 100 MB");
        return;
    }
    if (bytes < viewbbc_document_bytes_used(&editor->document)) {
        set_status(editor, "Buffer is smaller than current file");
        return;
    }

    /* Best-effort availability check. The document grows on demand, but reject
       a new maximum if the host allocator cannot reserve that much memory now. */
    void *probe = malloc(bytes);
    if (!probe) {
        set_status(editor, "Not enough free memory for requested buffer");
        return;
    }
    free(probe);

    if (!viewbbc_document_set_workspace_limit(&editor->document, bytes)) {
        set_status(editor, "Could not change buffer size");
        return;
    }
    editor->buffer_size_changed = 1;
    editor->buffer_size_requested = bytes;
    {
        char size_text[48];
        viewbbc_workspace_format_size(bytes, size_text, sizeof(size_text));
        (void)snprintf(editor->status_message, sizeof(editor->status_message),
                       "Buffer size %s", size_text);
    }
}


static void command_linenums(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!editor || !command) return;
    if (!command->has_argument) {
        (void)viewbbc_output_addf(&editor->command_output, "Line numbers: %s",
                                  editor->line_numbers ? "ON" : "OFF");
        set_status(editor, "");
        return;
    }
    int enabled;
    if (ascii_ieq(command->argument, "ON")) enabled = 1;
    else if (ascii_ieq(command->argument, "OFF")) enabled = 0;
    else {
        set_status(editor, "LINENUMS uses ON or OFF");
        return;
    }
    viewbbc_editor_set_line_numbers(editor, enabled);
    editor->line_numbers_changed = 1;
    editor->line_numbers_requested = enabled;
    (void)snprintf(editor->status_message, sizeof(editor->status_message),
                   "Line numbers %s", enabled ? "ON" : "OFF");
}

static void command_rowcols(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!editor || !command) return;
    if (!command->has_argument) {
        (void)viewbbc_output_addf(&editor->command_output, "Row/column display: %s",
                                  editor->rowcols ? "ON" : "OFF");
        set_status(editor, "");
        return;
    }
    int enabled;
    if (ascii_ieq(command->argument, "ON")) enabled = 1;
    else if (ascii_ieq(command->argument, "OFF")) enabled = 0;
    else {
        set_status(editor, "ROWCOLS uses ON or OFF");
        return;
    }
    viewbbc_editor_set_rowcols(editor, enabled);
    editor->rowcols_changed = 1;
    editor->rowcols_requested = enabled;
    (void)snprintf(editor->status_message, sizeof(editor->status_message),
                   "Row/column display %s", enabled ? "ON" : "OFF");
}

static void command_ver(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!editor || !command) return;
    if (command->has_argument) {
        set_status(editor, "VER takes no argument");
        return;
    }
    (void)viewbbc_output_addf(&editor->command_output, "%s %s",
                              VIEWBBC_PRODUCT_NAME, VIEWBBC_VERSION_STRING);
    set_status(editor, "");
}

static void append_command_result(ViewBBCEditor *editor) {
    if (!editor) return;

    if (editor->help_visible) build_help_output(editor);

    if (editor->command_output.line_count) {
        editor->output_page_next = 0;
        editor->output_paging = 0;
        editor->output_paging_continuous = 0;
        append_output_page(editor, 0);
        return;
    }

    /* Confirmation text is rendered live at the input cursor.  Do not also
       append it to console history, otherwise non-EXIT confirmations appear
       twice (once as history and once as the active prompt). */
    if (editor->confirm_action != VIEWBBC_CONFIRM_NONE) return;

    if (editor->status_message[0])
        (void)viewbbc_console_add(&editor->console, editor->status_message);
}

void viewbbc_editor_execute_command(ViewBBCEditor *editor) {
    if (!editor) return;

    (void)viewbbc_console_addf(&editor->console, "=>%s", editor->command_buffer);
    editor->output_paging = 0;
    editor->output_paging_continuous = 0;
    editor->output_page_next = 0;
    editor->help_visible = 0;
    editor->help_topic = VIEWBBC_HELP_TOPIC_ALL;
    viewbbc_output_clear(&editor->command_output);
    set_status(editor, "");

    ViewBBCParsedCommand command;
    if (!viewbbc_command_parse(editor->command_buffer, &command)) {
        set_status(editor, "Command or filename too long");
        append_command_result(editor);
        return;
    }

    switch (command.type) {
        case VIEWBBC_COMMAND_EMPTY:
            set_status(editor, "");
            break;
        case VIEWBBC_COMMAND_LOAD:
            command_load(editor, &command);
            break;
        case VIEWBBC_COMMAND_SAVE:
            command_save(editor, &command);
            break;
        case VIEWBBC_COMMAND_NEW:
            command_new(editor, &command);
            break;
        case VIEWBBC_COMMAND_READ:
        case VIEWBBC_COMMAND_WRITE:
        case VIEWBBC_COMMAND_COUNT:
        case VIEWBBC_COMMAND_SEARCH:
        case VIEWBBC_COMMAND_CHANGE:
        case VIEWBBC_COMMAND_REPLACE:
        case VIEWBBC_COMMAND_FOLD:
        case VIEWBBC_COMMAND_FORMAT:
        case VIEWBBC_COMMAND_SCREEN:
        case VIEWBBC_COMMAND_MODE:
            (void)viewbbc_view_command_execute(editor, &command);
            break;
        case VIEWBBC_COMMAND_SETUP:
            command_setup(editor, &command);
            break;
        case VIEWBBC_COMMAND_BUFFERSIZE:
            command_buffersize(editor, &command);
            break;
        case VIEWBBC_COMMAND_LINENUMS:
            command_linenums(editor, &command);
            break;
        case VIEWBBC_COMMAND_ROWCOLS:
            command_rowcols(editor, &command);
            break;
        case VIEWBBC_COMMAND_VER:
            command_ver(editor, &command);
            break;
        case VIEWBBC_COMMAND_PRINT:
            editor->print_requested = 1;
            editor->print_target_override[0] = '\0';
            if (command.has_argument)
                (void)snprintf(editor->print_target_override, sizeof(editor->print_target_override), "%s", command.argument);
            set_status(editor, "");
            break;
        case VIEWBBC_COMMAND_EXPORT:
            if (!command.has_argument) {
                set_status(editor, "EXPORT requires a filename");
            } else if (strlen(command.argument) + 5u >= sizeof(editor->print_target_override)) {
                set_status(editor, "Export filename too long");
            } else {
                editor->print_requested = 1;
                memcpy(editor->print_target_override, "file:", 5u);
                memcpy(editor->print_target_override + 5u, command.argument, strlen(command.argument) + 1u);
                set_status(editor, "");
            }
            break;
        case VIEWBBC_COMMAND_HELP:
            if (!command.has_argument) {
                set_status(editor, "");
                editor->help_visible = 1;
                editor->help_topic = VIEWBBC_HELP_TOPIC_ALL;
            } else if (viewbbc_help_topic_parse(command.argument, &editor->help_topic)) {
                set_status(editor, "");
                editor->help_visible = 1;
            } else {
                set_status(editor, "Unknown HELP subject - use FILE, EDIT, KEYS or CONF");
            }
            break;
        case VIEWBBC_COMMAND_CLEAR:
            if (command.has_argument) {
                set_status(editor, "CLEAR takes no parameters");
            } else {
                (void)viewbbc_markers_unset(&editor->markers, 1u);
                (void)viewbbc_markers_unset(&editor->markers, 2u);
                set_status(editor, "Markers 1 and 2 cleared");
            }
            break;
        case VIEWBBC_COMMAND_CLS:
            if (command.has_argument) {
                set_status(editor, "CLS takes no parameters");
            } else {
                viewbbc_console_clear(&editor->console);
                viewbbc_output_clear(&editor->command_output);
                editor->help_visible = 0;
                set_status(editor, "");
            }
            break;
        case VIEWBBC_COMMAND_CONFIG:
            if (!command.has_argument) {
                editor->config_mode_requested = 1;
                set_status(editor, "Opening configuration");
            } else if (ascii_ieq(command.argument, "LOC")) {
                (void)viewbbc_output_addf(&editor->command_output, "Configuration: %s",
                                          editor->config_path[0] ? editor->config_path : "unavailable");
                set_status(editor, "");
            } else if (ascii_ieq(command.argument, "RESET")) {
                (void)begin_confirmation(editor, VIEWBBC_CONFIRM_CONFIG_RESET, NULL,
                                         "Reset configuration to defaults? (Y/N)");
            } else {
                set_status(editor, "CONFIG accepts LOC, RESET or no parameters");
            }
            break;
        case VIEWBBC_COMMAND_MOUNT:
            command_mount(editor, &command);
            break;
        case VIEWBBC_COMMAND_UNMOUNT:
            command_unmount(editor, &command);
            break;
        case VIEWBBC_COMMAND_LIST:
        case VIEWBBC_COMMAND_DIR:
        case VIEWBBC_COMMAND_LS:
            command_list(editor, &command);
            break;
        case VIEWBBC_COMMAND_CD:
            command_cd(editor, &command);
            break;
        case VIEWBBC_COMMAND_EXIT:
        case VIEWBBC_COMMAND_QUIT:
            if (command.has_argument) set_status(editor, "EXIT/QUIT take no parameters");
            else viewbbc_editor_request_exit(editor);
            break;
        case VIEWBBC_COMMAND_UNKNOWN:
        case VIEWBBC_COMMAND_INVALID:
        default:
            set_status(editor, "Mistake");
            break;
    }

    append_command_result(editor);
}
