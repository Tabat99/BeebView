#define _XOPEN_SOURCE 700
#include "viewbbc/filesystem.h"
#include "viewbbc/commands.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

static char *duplicate_string(const char *text) {
    if (!text) return NULL;
    size_t n = strlen(text);
    if (n >= VIEWBBC_PATH_LIMIT || n == SIZE_MAX) return NULL;
    char *copy = malloc(n + 1u);
    if (!copy) return NULL;
    memcpy(copy, text, n + 1u);
    return copy;
}

static void set_error(char *error, size_t error_size, const char *message) {
    if (!error || error_size == 0) return;
    (void)snprintf(error, error_size, "%s", message ? message : "");
}

int viewbbc_fs_init(ViewBBCFilesystemState *state) {
    if (!state) return 0;
    *state = (ViewBBCFilesystemState){0};
    state->dfs_directory = '$';

    char cwd[VIEWBBC_PATH_LIMIT];
#ifdef _WIN32
    if (!_getcwd(cwd, (int)sizeof(cwd))) return 0;
#else
    if (!getcwd(cwd, sizeof(cwd))) return 0;
#endif
    state->native_cwd = duplicate_string(cwd);
    if (!state->native_cwd) return 0;
    return 1;
}

void viewbbc_fs_destroy(ViewBBCFilesystemState *state) {
    if (!state) return;
    free(state->native_cwd);
    *state = (ViewBBCFilesystemState){0};
}

static int normalize_native_path(const char *path, char **out) {
    if (!path || !out) return 0;
    *out = NULL;

    size_t n = strlen(path);
    if (n >= VIEWBBC_PATH_LIMIT || n == SIZE_MAX) return 0;

    char *copy = malloc(n + 1u);
    if (!copy) return 0;
    for (size_t i = 0; i < n; ++i) {
        copy[i] = path[i] == '\\' ? '/' : path[i];
    }
    copy[n] = '\0';
    *out = copy;
    return 1;
}

static int is_absolute_alias(const char *path) {
    if (!path) return 0;
#ifdef _WIN32
    if (isalpha((unsigned char)path[0]) && path[1] == ':' && path[2] == '/') return 1;
    if (path[0] == '/' && path[1] == '/') return 1; /* UNC path after normalization */
#endif
    return path[0] == '/';
}

static int join_paths(const char *base, const char *leaf, char **out) {
    if (!base || !leaf || !out) return 0;
    *out = NULL;
    size_t a = strlen(base);
    size_t b = strlen(leaf);
    int slash = a != 0 && base[a - 1] != '/';
    if (a >= VIEWBBC_PATH_LIMIT || b >= VIEWBBC_PATH_LIMIT) return 0;
    if (a > SIZE_MAX - b - (size_t)slash - 1u) return 0;
    size_t total = a + b + (size_t)slash;
    if (total >= VIEWBBC_PATH_LIMIT) return 0;
    char *p = malloc(total + 1u);
    if (!p) return 0;
    memcpy(p, base, a);
    size_t w = a;
    if (slash) p[w++] = '/';
    memcpy(p + w, leaf, b);
    p[w + b] = '\0';
    *out = p;
    return 1;
}

int viewbbc_fs_resolve_native(const ViewBBCFilesystemState *state,
                              const char *path,
                              char **path_out) {
    if (!state || !state->native_cwd || !path || !*path || !path_out) return 0;
    *path_out = NULL;

    char *normalized = NULL;
    if (!normalize_native_path(path, &normalized)) return 0;

    int ok = 0;
    if (normalized[0] == '~' && (normalized[1] == '\0' || normalized[1] == '/')) {
#ifdef _WIN32
        const char *home = getenv("USERPROFILE");
#else
        const char *home = getenv("HOME");
#endif
        if (home && *home) {
            if (normalized[1] == '\0') {
                *path_out = duplicate_string(home);
                ok = *path_out != NULL;
            } else {
                ok = join_paths(home, normalized + 2, path_out);
            }
        }
    } else if (is_absolute_alias(normalized)) {
        *path_out = duplicate_string(normalized);
        ok = *path_out != NULL;
    } else {
        ok = join_paths(state->native_cwd, normalized, path_out);
    }

    free(normalized);
    return ok;
}

int viewbbc_fs_chdir_native(ViewBBCFilesystemState *state,
                            const char *path,
                            char *error,
                            size_t error_size) {
    if (!state || !path || !*path) {
        set_error(error, error_size, "Missing path");
        return 0;
    }

    char *resolved = NULL;
    if (!viewbbc_fs_resolve_native(state, path, &resolved)) {
        set_error(error, error_size, "Path is too long or invalid");
        return 0;
    }

#ifdef _WIN32
    char full[VIEWBBC_PATH_LIMIT];
    char *canonical = NULL;
    if (_fullpath(full, resolved, sizeof(full)) != NULL) canonical = duplicate_string(full);
    free(resolved);
#else
    char *canonical = realpath(resolved, NULL);
    free(resolved);
#endif
    if (!canonical) {
        set_error(error, error_size, "Folder not found");
        return 0;
    }
    if (strlen(canonical) >= VIEWBBC_PATH_LIMIT) {
        free(canonical);
        set_error(error, error_size, "Path is too long");
        return 0;
    }

    struct stat st;
    if (stat(canonical, &st) != 0 || !S_ISDIR(st.st_mode)) {
        free(canonical);
        set_error(error, error_size, "Not a folder");
        return 0;
    }

    free(state->native_cwd);
    state->native_cwd = canonical;
    set_error(error, error_size, "");
    return 1;
}

#define VIEWBBC_NATIVE_LIST_MAX_ENTRIES 4096u
#define VIEWBBC_WIDE_TARGET_COLUMNS 78u
#define VIEWBBC_WIDE_MAX_COLUMNS 5u

typedef struct {
    char *name;
    uint64_t size;
    int is_directory;
    int stat_valid;
} ViewBBCNativeListEntry;

static int ascii_name_compare(const char *a, const char *b) {
    while (*a && *b) {
        unsigned char ca = (unsigned char)tolower((unsigned char)*a);
        unsigned char cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb) return ca < cb ? -1 : 1;
        ++a;
        ++b;
    }
    if (*a) return 1;
    if (*b) return -1;
    return strcmp(a, b);
}

static int native_entry_compare(const void *lhs, const void *rhs) {
    const ViewBBCNativeListEntry *a = lhs;
    const ViewBBCNativeListEntry *b = rhs;
    if (a->is_directory != b->is_directory) return a->is_directory ? -1 : 1;
    return ascii_name_compare(a->name, b->name);
}

static void native_entries_destroy(ViewBBCNativeListEntry *entries, size_t count) {
    if (!entries) return;
    for (size_t i = 0; i < count; ++i) free(entries[i].name);
    free(entries);
}

static int option_token_allowed(const char *token, ViewBBCListStyle style,
                                int *show_all, int *long_form, int *bare, int *wide) {
    if (!token || !*token) return 1;
    if (style == VIEWBBC_LIST_STYLE_LS) {
        if (token[0] != '-') return 0;
        for (size_t i = 1; token[i]; ++i) {
            if (token[i] == 'a') *show_all = 1;
            else if (token[i] == 'l') *long_form = 1;
            else return 0;
        }
        return 1;
    }
    if (style == VIEWBBC_LIST_STYLE_DIR) {
        if (token[0] != '/' && token[0] != '-') return 0;
        for (size_t i = 1; token[i]; ++i) {
            int ch = toupper((unsigned char)token[i]);
            if (ch == 'A') *show_all = 1;
            else if (ch == 'W') *wide = 1;
            else if (ch == 'B') *bare = 1;
            else return 0;
        }
        return 1;
    }
    return token[0] == '\0';
}

static int parse_options(const char *options, ViewBBCListStyle style,
                         int *show_all, int *long_form, int *bare, int *wide) {
    *show_all = 0;
    *long_form = 0;
    *bare = 0;
    *wide = 0;
    if (!options || !*options) return 1;

    size_t n = strlen(options);
    if (n > VIEWBBC_COMMAND_ARGUMENT_MAX) return 0;
    char buffer[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    memcpy(buffer, options, n + 1u);

    char *p = buffer;
    while (*p) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;
        char *start = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
        if (!option_token_allowed(start, style, show_all, long_form, bare, wide)) return 0;
    }
    return 1;
}

static int native_entries_add(ViewBBCNativeListEntry **entries,
                              size_t *count,
                              size_t *capacity,
                              const char *name,
                              int is_directory,
                              uint64_t size,
                              int stat_valid) {
    if (!entries || !count || !capacity || !name) return 0;
    if (*count >= VIEWBBC_NATIVE_LIST_MAX_ENTRIES) return 0;

    if (*count == *capacity) {
        size_t next = *capacity ? *capacity * 2u : 32u;
        if (next > VIEWBBC_NATIVE_LIST_MAX_ENTRIES) next = VIEWBBC_NATIVE_LIST_MAX_ENTRIES;
        if (next <= *capacity || next > SIZE_MAX / sizeof(**entries)) return 0;
        void *grown = realloc(*entries, next * sizeof(**entries));
        if (!grown) return 0;
        *entries = grown;
        *capacity = next;
    }

    char *name_copy = duplicate_string(name);
    if (!name_copy) return 0;
    (*entries)[*count] = (ViewBBCNativeListEntry){
        .name = name_copy,
        .size = size,
        .is_directory = is_directory,
        .stat_valid = stat_valid
    };
    (*count)++;
    return 1;
}

static int format_native_name(const ViewBBCNativeListEntry *entry,
                              ViewBBCListStyle style,
                              int bare,
                              char *out,
                              size_t out_size) {
    if (!entry || !entry->name || !out || out_size == 0) return 0;
    int written;
    if (bare) {
        written = snprintf(out, out_size, "%s", entry->name);
    } else if (entry->is_directory && style == VIEWBBC_LIST_STYLE_LS) {
        written = snprintf(out, out_size, "%s/", entry->name);
    } else if (entry->is_directory) {
        written = snprintf(out, out_size, "[%s]", entry->name);
    } else {
        written = snprintf(out, out_size, "%s", entry->name);
    }
    return written >= 0 && (size_t)written < out_size;
}

static void output_native_wide(ViewBBCCommandOutput *output,
                               const ViewBBCNativeListEntry *entries,
                               size_t count,
                               ViewBBCListStyle style) {
    size_t widest = 1u;
    char display[VIEWBBC_OUTPUT_LINE_MAX + 1];
    for (size_t i = 0; i < count; ++i) {
        if (!format_native_name(&entries[i], style, 0, display, sizeof(display))) {
            output->truncated = 1;
            continue;
        }
        size_t n = strlen(display);
        if (n > widest) widest = n;
    }

    size_t column_width = widest + 2u;
    if (column_width < 4u) column_width = 4u;
    size_t columns = VIEWBBC_WIDE_TARGET_COLUMNS / column_width;
    if (columns == 0) columns = 1;
    if (columns > VIEWBBC_WIDE_MAX_COLUMNS) columns = VIEWBBC_WIDE_MAX_COLUMNS;

    char line[VIEWBBC_OUTPUT_LINE_MAX + 1];
    for (size_t first = 0; first < count; first += columns) {
        size_t used = 0;
        line[used++] = ' ';
        line[used] = '\0';

        for (size_t col = 0; col < columns && first + col < count; ++col) {
            if (!format_native_name(&entries[first + col], style, 0, display, sizeof(display))) {
                output->truncated = 1;
                continue;
            }
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

int viewbbc_fs_list_native(const ViewBBCFilesystemState *state,
                           ViewBBCListStyle style,
                           const char *options,
                           ViewBBCCommandOutput *output,
                           char *error,
                           size_t error_size) {
    if (!state || !state->native_cwd || !output) return 0;

    int show_all, long_form, bare, wide;
    if (!parse_options(options, style, &show_all, &long_form, &bare, &wide)) {
        set_error(error, error_size, style == VIEWBBC_LIST_STYLE_DIR ? "Unsupported DIR option" : "Unsupported LS option");
        return 0;
    }

    DIR *dir = opendir(state->native_cwd);
    if (!dir) {
        set_error(error, error_size, "Cannot read folder");
        return 0;
    }

    ViewBBCNativeListEntry *entries = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int collection_truncated = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (!show_all && name[0] == '.') continue;

        char *full = NULL;
        int is_directory = 0;
        uint64_t size = 0;
        int stat_valid = 0;
        if (join_paths(state->native_cwd, name, &full)) {
            struct stat st;
            if (stat(full, &st) == 0) {
                stat_valid = 1;
                is_directory = S_ISDIR(st.st_mode) ? 1 : 0;
                if (st.st_size > 0) size = (uint64_t)st.st_size;
            }
            free(full);
        }

        if (!native_entries_add(&entries, &count, &capacity, name,
                                is_directory, size, stat_valid)) {
            collection_truncated = 1;
            break;
        }
    }
    closedir(dir);

    if (count > 1u) qsort(entries, count, sizeof(*entries), native_entry_compare);

    viewbbc_output_clear(output);
    output->truncated = collection_truncated;
    if (!bare) (void)viewbbc_output_addf(output, "%s", state->native_cwd);

    /* DIR /B is deliberately truly bare: no heading and no indentation. */
    if (wide && !bare && !long_form) {
        output_native_wide(output, entries, count, style);
    } else {
        for (size_t i = 0; i < count; ++i) {
            char display[VIEWBBC_OUTPUT_LINE_MAX + 1];
            if (!format_native_name(&entries[i], style, bare, display, sizeof(display))) {
                output->truncated = 1;
                continue;
            }

            if (long_form) {
                if (entries[i].stat_valid) {
                    (void)viewbbc_output_addf(output, " %c %10llu  %s",
                                              entries[i].is_directory ? 'd' : '-',
                                              (unsigned long long)entries[i].size,
                                              display);
                } else {
                    (void)viewbbc_output_addf(output, " %s", display);
                }
            } else if (bare) {
                (void)viewbbc_output_add(output, display);
            } else {
                (void)viewbbc_output_addf(output, " %s", display);
            }
        }
    }

    native_entries_destroy(entries, count);
    set_error(error, error_size, "");
    return 1;
}

static size_t common_prefix_len(const char *a, const char *b) {
    size_t n = 0;
    if (!a || !b) return 0;
    while (a[n] && b[n] && a[n] == b[n]) ++n;
    return n;
}

int viewbbc_fs_complete_native(const ViewBBCFilesystemState *state,
                               const char *path_prefix,
                               char *completion,
                               size_t completion_size,
                               size_t *match_count,
                               int *unique_is_directory,
                               ViewBBCCommandOutput *matches) {
    if (!state || !state->native_cwd || !path_prefix || !completion || completion_size == 0 || !match_count)
        return 0;

    completion[0] = '\0';
    *match_count = 0;
    if (unique_is_directory) *unique_is_directory = 0;
    if (matches) viewbbc_output_clear(matches);

    size_t input_len = strlen(path_prefix);
    if (input_len >= VIEWBBC_PATH_LIMIT) return 0;

    char normalized[VIEWBBC_PATH_LIMIT];
    for (size_t i = 0; i <= input_len; ++i)
        normalized[i] = path_prefix[i] == '\\' ? '/' : path_prefix[i];

    char *slash = strrchr(normalized, '/');
    const char *leaf = slash ? slash + 1 : normalized;
    char dir_input[VIEWBBC_PATH_LIMIT];
    char display_dir[VIEWBBC_PATH_LIMIT];
    if (slash) {
        size_t dlen = (size_t)(slash - normalized + 1);
        if (dlen >= sizeof(display_dir)) return 0;
        memcpy(display_dir, normalized, dlen);
        display_dir[dlen] = '\0';
        if (dlen == 1 && normalized[0] == '/') {
            memcpy(dir_input, "/", 2);
        } else {
            size_t bare_len = dlen - 1u;
            if (bare_len >= sizeof(dir_input)) return 0;
            memcpy(dir_input, normalized, bare_len);
            dir_input[bare_len] = '\0';
        }
    } else {
        memcpy(display_dir, "", 1);
        memcpy(dir_input, ".", 2);
    }

    char *resolved_dir = NULL;
    if (!viewbbc_fs_resolve_native(state, dir_input, &resolved_dir)) return 0;
    DIR *dir = opendir(resolved_dir);
    if (!dir) {
        free(resolved_dir);
        return 0;
    }

    char common[VIEWBBC_PATH_LIMIT] = {0};
    char sole[VIEWBBC_PATH_LIMIT] = {0};
    int sole_is_dir = 0;
    size_t leaf_len = strlen(leaf);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (strncmp(name, leaf, leaf_len) != 0) continue;

        char full[VIEWBBC_PATH_LIMIT];
        int nw = snprintf(full, sizeof(full), "%s%s", display_dir, name);
        if (nw < 0 || (size_t)nw >= sizeof(full)) continue;

        int is_dir = 0;
        char *full_native = NULL;
        if (join_paths(resolved_dir, name, &full_native)) {
            struct stat st;
            if (stat(full_native, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = 1;
            free(full_native);
        }

        if (*match_count == 0) {
            (void)snprintf(common, sizeof(common), "%s", full);
            (void)snprintf(sole, sizeof(sole), "%s", full);
            sole_is_dir = is_dir;
        } else {
            size_t cp = common_prefix_len(common, full);
            common[cp] = '\0';
        }
        (*match_count)++;

        if (matches) {
            char shown[VIEWBBC_PATH_LIMIT + 2];
            (void)snprintf(shown, sizeof(shown), "%s%s", full, is_dir ? "/" : "");
            (void)viewbbc_output_add(matches, shown);
        }
    }

    closedir(dir);
    free(resolved_dir);

    if (*match_count == 0) return 1;
    const char *chosen = *match_count == 1 ? sole : common;
    if (snprintf(completion, completion_size, "%s", chosen) < 0 || strlen(chosen) >= completion_size) {
        completion[0] = '\0';
        return 0;
    }
    if (*match_count == 1 && unique_is_directory) *unique_is_directory = sole_is_dir;
    return 1;
}
