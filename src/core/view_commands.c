#include "viewbbc/view_commands.h"

#include "viewbbc/dfs.h"
#include "viewbbc/file_io.h"
#include "viewbbc/filesystem.h"
#include "viewbbc/formatter.h"
#include "viewbbc/output.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_status(ViewBBCEditor *editor, const char *message) {
    if (!editor) return;
    if (!message) message = "";
    (void)snprintf(editor->status_message, sizeof(editor->status_message), "%s", message);
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

    int n = snprintf(out, out_size, "%s%c.%s",
                     drive_prefix, editor->filesystem.dfs_directory, name);
    return n >= 0 && (size_t)n < out_size;
}

static int parse_next_text_argument(const char **cursor, char *out, size_t out_size) {
    if (!cursor || !*cursor || !out || out_size == 0) return 0;
    const char *p = *cursor;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (!*p) {
        out[0] = '\0';
        *cursor = p;
        return 0;
    }

    size_t w = 0;
    if (*p == '"') {
        ++p;
        while (*p) {
            if (*p == '"') {
                if (p[1] == '"') {
                    if (w + 1u >= out_size) return -1;
                    out[w++] = '"';
                    p += 2;
                    continue;
                }
                ++p;
                break;
            }
            if (w + 1u >= out_size) return -1;
            out[w++] = *p++;
        }
    } else {
        while (*p && !isspace((unsigned char)*p)) {
            if (w + 1u >= out_size) return -1;
            out[w++] = *p++;
        }
    }
    out[w] = '\0';
    *cursor = p;
    return w ? 1 : 0;
}



typedef struct {
    char filename[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    unsigned first_marker;
    unsigned second_marker;
    int marker_count;
} ViewBBCFileMarkerSpec;

static int parse_file_marker_spec(const char *argument, int wanted_markers,
                                  ViewBBCFileMarkerSpec *spec) {
    if (!argument || !spec || wanted_markers < 0 || wanted_markers > 2) return 0;
    *spec = (ViewBBCFileMarkerSpec){0};
    size_t length = strlen(argument);
    while (length && isspace((unsigned char)argument[length - 1u])) --length;
    if (!length) return 0;

    size_t cut = length;
    unsigned markers[2] = {0, 0};
    int found = 0;
    while (found < wanted_markers) {
        size_t end = cut;
        while (end && isspace((unsigned char)argument[end - 1u])) --end;
        size_t start = end;
        while (start && !isspace((unsigned char)argument[start - 1u])) --start;
        if (end - start != 1u || argument[start] < '1' || argument[start] > '6') break;
        markers[wanted_markers - 1 - found] = (unsigned)(argument[start] - '0');
        cut = start;
        ++found;
    }
    if (wanted_markers == 2 && found != 2) return 0;
    while (cut && isspace((unsigned char)argument[cut - 1u])) --cut;
    if (!cut) return 0;

    char raw[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    if (cut >= sizeof(raw)) return 0;
    memcpy(raw, argument, cut);
    raw[cut] = '\0';
    int has_filename = 0;
    if (!viewbbc_command_decode_argument(raw, spec->filename, sizeof(spec->filename), &has_filename) || !has_filename)
        return 0;
    spec->marker_count = found;
    if (found == 1) spec->first_marker = markers[wanted_markers - 1];
    if (found == 2) {
        spec->first_marker = markers[0];
        spec->second_marker = markers[1];
    }
    return 1;
}

typedef struct {
    char from[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    char to[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    int has_range;
    unsigned start_marker;
    unsigned end_marker;
} ViewBBCChangeSpec;

static int parse_change_spec(const char *text, ViewBBCChangeSpec *spec) {
    if (!text || !spec) return 0;
    *spec = (ViewBBCChangeSpec){0};
    const char *p = text;
    int a = parse_next_text_argument(&p, spec->from, sizeof(spec->from));
    if (a <= 0) return a;
    int b = parse_next_text_argument(&p, spec->to, sizeof(spec->to));
    if (b <= 0) return b;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (!*p) return 1;
    if (p[0] < '1' || p[0] > '6') return 0;
    spec->start_marker = (unsigned)(p[0] - '0');
    ++p;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (p[0] < '1' || p[0] > '6') return 0;
    spec->end_marker = (unsigned)(p[0] - '0');
    ++p;
    while (*p && isspace((unsigned char)*p)) ++p;
    if (*p) return 0;
    spec->has_range = 1;
    return 1;
}


typedef struct {
    char text[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    int has_range;
    unsigned start_marker;
    unsigned end_marker;
} ViewBBCSearchSpec;

static int position_compare(size_t line_a, size_t column_a,
                            size_t line_b, size_t column_b) {
    if (line_a < line_b) return -1;
    if (line_a > line_b) return 1;
    if (column_a < column_b) return -1;
    if (column_a > column_b) return 1;
    return 0;
}

static int marker_range(const ViewBBCEditor *editor, unsigned first, unsigned second,
                        size_t *start_line, size_t *start_column,
                        size_t *end_line, size_t *end_column) {
    if (!editor || !start_line || !start_column || !end_line || !end_column) return 0;
    if (!viewbbc_markers_get(&editor->markers, first, start_line, start_column) ||
        !viewbbc_markers_get(&editor->markers, second, end_line, end_column)) return 0;
    return position_compare(*start_line, *start_column, *end_line, *end_column) < 0;
}

static int parse_marker_pair(const char *text, unsigned *first, unsigned *second) {
    if (!text || !first || !second) return 0;
    while (*text && isspace((unsigned char)*text)) ++text;
    if (text[0] < '1' || text[0] > '6') return 0;
    *first = (unsigned)(text[0] - '0');
    ++text;
    while (*text && isspace((unsigned char)*text)) ++text;
    if (text[0] < '1' || text[0] > '6') return 0;
    *second = (unsigned)(text[0] - '0');
    ++text;
    while (*text && isspace((unsigned char)*text)) ++text;
    return *text == '\0';
}

static int decode_search_text(const char *start, size_t length, char *out, size_t out_size) {
    while (length && isspace((unsigned char)*start)) { ++start; --length; }
    while (length && isspace((unsigned char)start[length - 1u])) --length;
    if (!length || !out || out_size == 0) return 0;
    if (length >= 2u && start[0] == '"' && start[length - 1u] == '"') {
        size_t w = 0;
        for (size_t i = 1u; i + 1u < length; ++i) {
            char ch = start[i];
            if (ch == '"' && i + 2u < length && start[i + 1u] == '"') ++i;
            if (w + 1u >= out_size) return 0;
            out[w++] = ch;
        }
        out[w] = '\0';
        return w != 0;
    }
    if (length + 1u > out_size) return 0;
    memcpy(out, start, length);
    out[length] = '\0';
    return 1;
}

static int parse_search_spec(const char *argument, ViewBBCSearchSpec *spec) {
    if (!argument || !spec) return 0;
    *spec = (ViewBBCSearchSpec){0};
    size_t length = strlen(argument);
    while (length && isspace((unsigned char)argument[length - 1u])) --length;
    if (!length) return 0;

    /* VIEW's limited-search suffix is two marker numbers at the end. */
    size_t end = length;
    size_t token2_end = end;
    size_t token2_start = token2_end;
    while (token2_start && !isspace((unsigned char)argument[token2_start - 1u])) --token2_start;
    size_t before2 = token2_start;
    while (before2 && isspace((unsigned char)argument[before2 - 1u])) --before2;
    size_t token1_end = before2;
    size_t token1_start = token1_end;
    while (token1_start && !isspace((unsigned char)argument[token1_start - 1u])) --token1_start;

    if (token1_end - token1_start == 1u && token2_end - token2_start == 1u &&
        argument[token1_start] >= '1' && argument[token1_start] <= '6' &&
        argument[token2_start] >= '1' && argument[token2_start] <= '6' && token1_start > 0u) {
        size_t text_end = token1_start;
        while (text_end && isspace((unsigned char)argument[text_end - 1u])) --text_end;
        if (!decode_search_text(argument, text_end, spec->text, sizeof(spec->text))) return 0;
        spec->has_range = 1;
        spec->start_marker = (unsigned)(argument[token1_start] - '0');
        spec->end_marker = (unsigned)(argument[token2_start] - '0');
        return 1;
    }

    return decode_search_text(argument, length, spec->text, sizeof(spec->text));
}

static size_t count_words_range(const ViewBBCDocument *doc,
                                size_t start_line, size_t start_column,
                                size_t end_line, size_t end_column) {
    if (!doc || position_compare(start_line, start_column, end_line, end_column) >= 0) return 0;
    size_t count = 0;
    int in_word = 0;
    for (size_t line = start_line; line <= end_line && line < doc->line_count; ++line) {
        const ViewBBCLine *src = &doc->lines[line];
        size_t first = line == start_line ? start_column : 0u;
        size_t last = line == end_line ? end_column : src->length;
        if (first > src->length) first = src->length;
        if (last > src->length) last = src->length;
        if (last < first) last = first;
        for (size_t column = first; column < last; ++column) {
            uint8_t ch = src->data[column];
            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') in_word = 0;
            else if (!in_word) { ++count; in_word = 1; }
        }
        in_word = 0;
        if (line == end_line) break;
    }
    return count;
}

static int find_limited(const ViewBBCDocument *doc,
                        const uint8_t *needle, size_t needle_length,
                        size_t start_line, size_t start_column,
                        size_t end_line, size_t end_column,
                        size_t *line_out, size_t *column_out) {
    if (!doc || !needle || needle_length == 0 || !line_out || !column_out ||
        start_line >= doc->line_count ||
        position_compare(start_line, start_column, end_line, end_column) >= 0) return 0;
    for (size_t line = start_line; line < doc->line_count; ++line) {
        if (line > end_line) break;
        const ViewBBCLine *src = &doc->lines[line];
        size_t first = line == start_line ? start_column : 0u;
        size_t last = line == end_line ? end_column : src->length;
        if (first > src->length) first = src->length;
        if (last > src->length) last = src->length;
        if (needle_length > last || first > last - needle_length) {
            if (line == end_line) break;
            continue;
        }
        for (size_t column = first; column <= last - needle_length; ++column) {
            if (memcmp(src->data + column, needle, needle_length) == 0) {
                *line_out = line;
                *column_out = column;
                return 1;
            }
        }
        if (line == end_line) break;
    }
    return 0;
}


static int byte_eq_fold(uint8_t a, uint8_t b, int fold) {
    if (!fold) return a == b;
    if (a < 128u && b < 128u)
        return toupper((unsigned char)a) == toupper((unsigned char)b);
    return a == b;
}

static int find_change_limited(const ViewBBCDocument *doc,
                               const uint8_t *needle, size_t needle_length,
                               size_t start_line, size_t start_column,
                               size_t end_line, size_t end_column,
                               int fold,
                               size_t *line_out, size_t *column_out) {
    if (!doc || !needle || needle_length == 0 || !line_out || !column_out ||
        start_line >= doc->line_count ||
        position_compare(start_line, start_column, end_line, end_column) >= 0) return 0;
    for (size_t line = start_line; line < doc->line_count; ++line) {
        if (line > end_line) break;
        const ViewBBCLine *src = &doc->lines[line];
        size_t first = line == start_line ? start_column : 0u;
        size_t last = line == end_line ? end_column : src->length;
        if (first > src->length) first = src->length;
        if (last > src->length) last = src->length;
        if (needle_length <= last && first <= last - needle_length) {
            for (size_t column = first; column <= last - needle_length; ++column) {
                size_t i = 0;
                for (; i < needle_length; ++i) {
                    if (!byte_eq_fold(src->data[column + i], needle[i], fold)) break;
                }
                if (i == needle_length) {
                    *line_out = line;
                    *column_out = column;
                    return 1;
                }
            }
        }
        if (line == end_line) break;
    }
    return 0;
}

static size_t folded_replacement(const ViewBBCLine *line, size_t column, size_t old_length,
                                 const char *replacement, size_t replacement_length,
                                 int fold, uint8_t *out, size_t out_size) {
    if (!line || !replacement || !out || replacement_length > out_size) return 0;
    int all_upper = fold && old_length != 0u;
    int all_lower = fold && old_length != 0u;
    int title_case = fold && old_length != 0u;
    for (size_t i = 0; i < old_length && column + i < line->length; ++i) {
        uint8_t old = line->data[column + i];
        if (old >= 128u || !isalpha((unsigned char)old)) continue;
        if (!isupper((unsigned char)old)) all_upper = 0;
        if (!islower((unsigned char)old)) all_lower = 0;
        if (i == 0u) {
            if (!isupper((unsigned char)old)) title_case = 0;
        } else if (!islower((unsigned char)old)) title_case = 0;
    }
    for (size_t i = 0; i < replacement_length; ++i) {
        uint8_t ch = (uint8_t)replacement[i];
        if (fold && ch < 128u && isalpha((unsigned char)ch)) {
            if (all_upper) ch = (uint8_t)toupper((unsigned char)ch);
            else if (all_lower) ch = (uint8_t)tolower((unsigned char)ch);
            else if (title_case) ch = (uint8_t)(i == 0u
                ? toupper((unsigned char)ch) : tolower((unsigned char)ch));
            else if (i < old_length && column + i < line->length) {
                uint8_t old = line->data[column + i];
                if (old < 128u && isalpha((unsigned char)old))
                    ch = (uint8_t)(isupper((unsigned char)old)
                        ? toupper((unsigned char)ch) : tolower((unsigned char)ch));
            }
        }
        out[i] = ch;
    }
    return replacement_length;
}


static void adjust_markers_after_same_line_replace(ViewBBCMarkers *markers,
                                                   size_t line, size_t column,
                                                   size_t old_length, size_t new_length) {
    if (!markers) return;
    size_t old_end = column + old_length;
    for (unsigned n = 1u; n <= VIEWBBC_MARKER_COUNT; ++n) {
        size_t ml = 0, mc = 0;
        if (!viewbbc_markers_get(markers, n, &ml, &mc) || ml != line) continue;
        if (mc >= old_end) {
            if (new_length >= old_length) mc += new_length - old_length;
            else mc -= old_length - new_length;
            (void)viewbbc_markers_set(markers, n, ml, mc);
        } else if (mc > column) {
            (void)viewbbc_markers_set(markers, n, ml, column);
        }
    }
}

static int document_end(const ViewBBCDocument *doc, size_t *line, size_t *column) {
    if (!doc || !line || !column || doc->line_count == 0u) return 0;
    *line = doc->line_count - 1u;
    *column = doc->lines[*line].length;
    return 1;
}

static int change_range(const ViewBBCEditor *editor, int has_range,
                        unsigned start_marker, unsigned end_marker,
                        size_t *start_line, size_t *start_column,
                        size_t *end_line, size_t *end_column) {
    if (has_range)
        return marker_range(editor, start_marker, end_marker,
                            start_line, start_column, end_line, end_column);
    *start_line = 0u;
    *start_column = 0u;
    return document_end(&editor->document, end_line, end_column);
}

static int read_active_file(ViewBBCEditor *editor, const char *name,
                            uint8_t **data_out, size_t *length_out) {
    if (!editor || !name || !*name || !data_out || !length_out) return 0;
    *data_out = NULL;
    *length_out = 0;

    if (editor->image_mounted) {
        char qualified[64];
        if (!dfs_qualify_name(editor, name, qualified, sizeof(qualified))) return 0;
        const ViewBBCDFSFile *file = viewbbc_dfs_find_file(&editor->mounted_image, qualified);
        if (!file) return 0;
        return viewbbc_dfs_extract_file(&editor->mounted_image, file, data_out, length_out);
    }

    char *path = NULL;
    if (!viewbbc_fs_resolve_native(&editor->filesystem, name, &path)) return 0;

    size_t free_bytes = viewbbc_document_bytes_free(&editor->document);
    size_t max_read;
    if (free_bytes > (SIZE_MAX - 1u) / 2u) max_read = VIEWBBC_HOST_READ_LIMIT;
    else max_read = free_bytes * 2u + 1u; /* allow CRLF before normalisation */
    if (max_read > VIEWBBC_HOST_READ_LIMIT) max_read = VIEWBBC_HOST_READ_LIMIT;
    if (max_read == 0) max_read = 1u;

    int ok = viewbbc_file_read_all(path, data_out, length_out, max_read);
    free(path);
    return ok;
}

static void command_read(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        set_status(editor, "READ needs a filename");
        return;
    }

    ViewBBCFileMarkerSpec spec;
    if (!parse_file_marker_spec(command->argument, 1, &spec)) {
        /* No marker suffix: preserve BeebView's filenames-with-spaces extension. */
        int has_filename = 0;
        spec = (ViewBBCFileMarkerSpec){0};
        if (!viewbbc_command_decode_argument(command->argument, spec.filename,
                                             sizeof(spec.filename), &has_filename) || !has_filename) {
            set_status(editor, "Bad READ command");
            return;
        }
    }

    size_t line = editor->cursor_line, column = editor->cursor_column;
    if (spec.marker_count == 1) {
        if (!viewbbc_markers_get(&editor->markers, spec.first_marker, &line, &column)) {
            set_status(editor, "READ marker unset");
            return;
        }
    }

    uint8_t *data = NULL;
    size_t length = 0;
    if (!read_active_file(editor, spec.filename, &data, &length)) {
        set_status(editor, "Cannot read file");
        return;
    }

    size_t before = viewbbc_document_bytes_used(&editor->document);
    int ok = viewbbc_document_insert_bytes_at(&editor->document, line, column, data, length);
    free(data);
    if (!ok) {
        set_status(editor, "Cannot READ file - no room or invalid text");
        return;
    }
    if (viewbbc_document_bytes_used(&editor->document) != before) editor->file_modified = 1;
    set_status(editor, spec.marker_count ? "File read at marker" : "File read into text");
}

static void command_write(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        set_status(editor, "WRITE needs a filename and two markers");
        return;
    }
    if (editor->image_mounted) {
        set_status(editor, "Mounted DFS image is read only");
        return;
    }

    ViewBBCFileMarkerSpec spec;
    if (!parse_file_marker_spec(command->argument, 2, &spec)) {
        set_status(editor, "WRITE needs filename m1 m2");
        return;
    }
    size_t start_line = 0, start_column = 0, end_line = 0, end_column = 0;
    if (!marker_range(editor, spec.first_marker, spec.second_marker,
                      &start_line, &start_column, &end_line, &end_column)) {
        set_status(editor, "WRITE markers unset or invalid");
        return;
    }

    char *path = NULL;
    if (!viewbbc_fs_resolve_native(&editor->filesystem, spec.filename, &path)) {
        set_status(editor, "Bad filename or path");
        return;
    }
    int ok = viewbbc_file_save_range(path, &editor->document,
                                     start_line, start_column, end_line, end_column);
    free(path);
    set_status(editor, ok ? "Marked text written" : "Cannot write file");
}

static void command_count(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    size_t words = 0;
    if (!command->has_argument) {
        words = viewbbc_document_count_words(&editor->document);
    } else {
        unsigned first = 0, second = 0;
        size_t start_line = 0, start_column = 0, end_line = 0, end_column = 0;
        if (!parse_marker_pair(command->argument, &first, &second)) {
            set_status(editor, "COUNT needs two marker numbers");
            return;
        }
        if (!marker_range(editor, first, second, &start_line, &start_column, &end_line, &end_column)) {
            set_status(editor, "COUNT markers unset or invalid");
            return;
        }
        words = count_words_range(&editor->document, start_line, start_column, end_line, end_column);
    }
    (void)viewbbc_output_addf(&editor->command_output, "Words: %zu", words);
    set_status(editor, "");
}

static int run_search(ViewBBCEditor *editor, size_t start_line, size_t start_column) {
    if (!editor || editor->search_length == 0u) return 0;
    size_t end_line = viewbbc_document_line_count(&editor->document);
    if (end_line == 0u) return 0;
    --end_line;
    size_t end_column = viewbbc_document_line_length(&editor->document, end_line);
    if (editor->search_range_active) {
        size_t range_start_line = 0, range_start_column = 0;
        if (!marker_range(editor, editor->search_start_marker, editor->search_end_marker,
                          &range_start_line, &range_start_column, &end_line, &end_column)) {
            set_status(editor, "SEARCH markers unset or invalid");
            return 0;
        }
        if (position_compare(start_line, start_column, range_start_line, range_start_column) < 0) {
            start_line = range_start_line;
            start_column = range_start_column;
        }
    }

    size_t line = 0, column = 0;
    if (!find_limited(&editor->document, (const uint8_t *)editor->search_text, editor->search_length,
                      start_line, start_column, end_line, end_column, &line, &column)) {
        set_status(editor, "Not found");
        return 0;
    }
    editor->cursor_line = line;
    editor->cursor_column = column;
    editor->viewport_top = line;
    editor->viewport_left = 0;
    editor->mode = VIEWBBC_MODE_TEXT;
    set_status(editor, "Found");
    return 1;
}

static void command_search(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument || command->argument[0] == '\0') {
        set_status(editor, "SEARCH needs text");
        return;
    }

    ViewBBCSearchSpec spec;
    if (!parse_search_spec(command->argument, &spec) || spec.text[0] == '\0') {
        set_status(editor, "Bad SEARCH command");
        return;
    }
    size_t start_line = 0u, start_column = 0u;
    if (spec.has_range) {
        size_t end_line = 0u, end_column = 0u;
        if (!marker_range(editor, spec.start_marker, spec.end_marker,
                          &start_line, &start_column, &end_line, &end_column)) {
            set_status(editor, "SEARCH markers unset or invalid");
            return;
        }
    }
    (void)snprintf(editor->search_text, sizeof(editor->search_text), "%s", spec.text);
    editor->search_length = strlen(editor->search_text);
    editor->search_range_active = spec.has_range;
    editor->search_start_marker = spec.start_marker;
    editor->search_end_marker = spec.end_marker;
    (void)run_search(editor, start_line, start_column);
}

static int apply_one_replacement(ViewBBCEditor *editor,
                                 size_t line, size_t column,
                                 size_t from_length,
                                 const char *to, size_t to_length,
                                 int fold) {
    uint8_t replacement[VIEWBBC_COMMAND_ARGUMENT_MAX + 1];
    if (to_length > VIEWBBC_COMMAND_ARGUMENT_MAX) return 0;
    if (to_length && folded_replacement(&editor->document.lines[line], column, from_length,
                                        to, to_length, fold,
                                        replacement, sizeof(replacement)) != to_length) return 0;
    if (!viewbbc_document_replace_at(&editor->document, line, column, from_length,
                                     replacement, to_length)) return 0;
    adjust_markers_after_same_line_replace(&editor->markers, line, column, from_length, to_length);
    return 1;
}

static void command_change(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        set_status(editor, "CHANGE needs old and new text");
        return;
    }

    ViewBBCChangeSpec spec;
    int parsed = parse_change_spec(command->argument, &spec);
    if (parsed < 0) {
        set_status(editor, "CHANGE text is too long");
        return;
    }
    if (!parsed || spec.from[0] == '\0') {
        set_status(editor, "Bad CHANGE command");
        return;
    }

    size_t from_length = strlen(spec.from), to_length = strlen(spec.to);
    size_t line = 0, column = 0, end_line = 0, end_column = 0;
    if (!change_range(editor, spec.has_range, spec.start_marker, spec.end_marker,
                      &line, &column, &end_line, &end_column)) {
        set_status(editor, "CHANGE markers unset or invalid");
        return;
    }

    size_t count = 0;
    while (1) {
        if (spec.has_range && !marker_range(editor, spec.start_marker, spec.end_marker,
                                            &(size_t){0}, &(size_t){0}, &end_line, &end_column)) {
            set_status(editor, "CHANGE markers unset or invalid");
            return;
        } else if (!spec.has_range && !document_end(&editor->document, &end_line, &end_column)) break;
        size_t found_line = 0, found_column = 0;
        if (!find_change_limited(&editor->document, (const uint8_t *)spec.from, from_length,
                                 line, column, end_line, end_column, editor->fold_mode,
                                 &found_line, &found_column)) break;
        if (!apply_one_replacement(editor, found_line, found_column,
                                   from_length, spec.to, to_length,
                                   editor->fold_mode)) {
            set_status(editor, "Cannot CHANGE - no room");
            return;
        }
        ++count;
        line = found_line;
        column = found_column + to_length;
        if (column > viewbbc_document_line_length(&editor->document, line)) {
            if (line + 1u >= viewbbc_document_line_count(&editor->document)) break;
            ++line; column = 0u;
        }
    }
    if (count) editor->file_modified = 1;
    (void)viewbbc_output_addf(&editor->command_output, "Changed: %zu", count);
    set_status(editor, "");
}

static int run_replace_search(ViewBBCEditor *editor, size_t start_line, size_t start_column) {
    size_t end_line = 0, end_column = 0;
    if (!change_range(editor, editor->search_range_active,
                      editor->search_start_marker, editor->search_end_marker,
                      &(size_t){0}, &(size_t){0}, &end_line, &end_column)) {
        set_status(editor, "REPLACE markers unset or invalid");
        editor->replace_active = 0;
        return 0;
    }
    size_t line = 0, column = 0;
    if (!find_change_limited(&editor->document, (const uint8_t *)editor->search_text,
                             editor->search_length, start_line, start_column,
                             end_line, end_column, editor->fold_mode, &line, &column)) {
        set_status(editor, "No more matches");
        editor->replace_active = 0;
        editor->mode = VIEWBBC_MODE_COMMAND;
        return 0;
    }
    editor->cursor_line = line;
    editor->cursor_column = column;
    editor->viewport_top = line;
    editor->viewport_left = 0;
    editor->mode = VIEWBBC_MODE_TEXT;
    set_status(editor, "Replace? Y/N");
    return 1;
}

static void command_replace(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        set_status(editor, "REPLACE needs old and new text");
        return;
    }
    ViewBBCChangeSpec spec;
    int parsed = parse_change_spec(command->argument, &spec);
    if (parsed < 0) { set_status(editor, "REPLACE text is too long"); return; }
    if (!parsed || spec.from[0] == '\0') { set_status(editor, "Bad REPLACE command"); return; }

    size_t start_line = 0u, start_column = 0u, end_line = 0u, end_column = 0u;
    if (!change_range(editor, spec.has_range, spec.start_marker, spec.end_marker,
                      &start_line, &start_column, &end_line, &end_column)) {
        set_status(editor, "REPLACE markers unset or invalid");
        return;
    }
    (void)snprintf(editor->search_text, sizeof(editor->search_text), "%s", spec.from);
    editor->search_length = strlen(editor->search_text);
    (void)snprintf(editor->replace_text, sizeof(editor->replace_text), "%s", spec.to);
    editor->replace_length = strlen(editor->replace_text);
    editor->search_range_active = spec.has_range;
    editor->search_start_marker = spec.start_marker;
    editor->search_end_marker = spec.end_marker;
    editor->replace_active = 1;
    (void)run_replace_search(editor, start_line, start_column);
}

static void command_fold(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        (void)viewbbc_output_addf(&editor->command_output, "Fold %d", editor->fold_mode ? 1 : 0);
        set_status(editor, "");
        return;
    }
    if (strcmp(command->argument, "0") == 0) editor->fold_mode = 0;
    else if (strcmp(command->argument, "1") == 0) editor->fold_mode = 1;
    else { set_status(editor, "FOLD takes 0 or 1"); return; }
    (void)viewbbc_output_addf(&editor->command_output, "Fold %d", editor->fold_mode ? 1 : 0);
    set_status(editor, "");
}


static void command_screen(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (command->has_argument) {
        set_status(editor, "SCREEN takes no parameters");
        return;
    }

    size_t lines = viewbbc_document_line_count(&editor->document);
    for (size_t i = 0; i < lines; ++i) {
        const ViewBBCLine *line = &editor->document.lines[i];
        if (line->length == 0) {
            (void)viewbbc_output_add(&editor->command_output, "");
            continue;
        }
        size_t pos = 0;
        while (pos < line->length) {
            size_t chunk = line->length - pos;
            if (chunk > VIEWBBC_OUTPUT_LINE_MAX) chunk = VIEWBBC_OUTPUT_LINE_MAX;
            char text[VIEWBBC_OUTPUT_LINE_MAX + 1];
            memcpy(text, line->data + pos, chunk);
            text[chunk] = '\0';
            if (!viewbbc_output_add(&editor->command_output, text)) break;
            pos += chunk;
        }
        if (editor->command_output.truncated) break;
    }
    set_status(editor, "");
}

static void command_mode(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!command->has_argument) {
        (void)viewbbc_output_addf(&editor->command_output,
                                  "Screen mode %d", editor->screen_mode);
        set_status(editor, "");
        return;
    }
    if (strcmp(command->argument, "3") != 0) {
        set_status(editor, "Only MODE 3 is supported at present");
        return;
    }
    editor->screen_mode = 3;
    set_status(editor, "Screen mode 3");
}


static void command_format(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    unsigned first = 0, second = 0;
    int has_range = 0;
    if (command->has_argument) {
        if (!parse_marker_pair(command->argument, &first, &second)) {
            set_status(editor, "FORMAT needs zero or two marker numbers");
            return;
        }
        size_t sl, sc, el, ec;
        if (!marker_range(editor, first, second, &sl, &sc, &el, &ec)) {
            set_status(editor, "FORMAT markers unset or invalid");
            return;
        }
        has_range = 1;
    }
    ViewBBCFormatResult result = viewbbc_format_global(&editor->document, &editor->markers,
                                                        has_range, first, second,
                                                        editor->justify_mode,
                                                        &editor->cursor_line, &editor->cursor_column);
    if (result == VIEWBBC_FORMAT_OK) {
        editor->file_modified = 1;
        set_status(editor, "Formatted");
    } else if (result == VIEWBBC_FORMAT_NO_MEMORY) {
        set_status(editor, "Cannot FORMAT - no room");
    } else {
        set_status(editor, "Nothing to FORMAT");
    }
}

int viewbbc_view_command_execute(ViewBBCEditor *editor, const ViewBBCParsedCommand *command) {
    if (!editor || !command) return 0;
    switch (command->type) {
        case VIEWBBC_COMMAND_READ:   command_read(editor, command); return 1;
        case VIEWBBC_COMMAND_WRITE:  command_write(editor, command); return 1;
        case VIEWBBC_COMMAND_COUNT:  command_count(editor, command); return 1;
        case VIEWBBC_COMMAND_SEARCH: command_search(editor, command); return 1;
        case VIEWBBC_COMMAND_CHANGE: command_change(editor, command); return 1;
        case VIEWBBC_COMMAND_REPLACE: command_replace(editor, command); return 1;
        case VIEWBBC_COMMAND_FOLD:    command_fold(editor, command); return 1;
        case VIEWBBC_COMMAND_FORMAT:  command_format(editor, command); return 1;
        case VIEWBBC_COMMAND_SCREEN: command_screen(editor, command); return 1;
        case VIEWBBC_COMMAND_MODE:   command_mode(editor, command); return 1;
        default: return 0;
    }
}

int viewbbc_view_next_match(ViewBBCEditor *editor) {
    if (!editor || editor->search_length == 0u) {
        if (editor) set_status(editor, "No active SEARCH");
        return 0;
    }
    size_t line = editor->cursor_line;
    size_t column = editor->cursor_column;
    size_t length = viewbbc_document_line_length(&editor->document, line);
    size_t advance = editor->replace_active ? editor->search_length : 1u;
    if (column <= length && advance <= length - column) column += advance;
    else if (line + 1u < viewbbc_document_line_count(&editor->document)) { ++line; column = 0u; }
    else {
        set_status(editor, editor->replace_active ? "No more matches" : "Not found");
        if (editor->replace_active) { editor->replace_active = 0; editor->mode = VIEWBBC_MODE_COMMAND; }
        return 0;
    }
    if (editor->replace_active) return run_replace_search(editor, line, column);
    return run_search(editor, line, column);
}

int viewbbc_view_replace_response(ViewBBCEditor *editor, int replace) {
    if (!editor || !editor->replace_active || editor->search_length == 0u) return 0;
    size_t line = editor->cursor_line;
    size_t column = editor->cursor_column;
    if (replace) {
        if (!apply_one_replacement(editor, line, column,
                                   editor->search_length,
                                   editor->replace_text, editor->replace_length,
                                   editor->fold_mode)) {
            set_status(editor, "Cannot REPLACE - no room");
            editor->replace_active = 0;
            editor->mode = VIEWBBC_MODE_COMMAND;
            return 0;
        }
        editor->file_modified = 1;
        column += editor->replace_length;
    } else {
        column += editor->search_length;
    }
    size_t length = viewbbc_document_line_length(&editor->document, line);
    if (column > length) column = length;
    if (column == length && line + 1u < viewbbc_document_line_count(&editor->document)) {
        ++line; column = 0u;
    }
    return run_replace_search(editor, line, column);
}

void viewbbc_view_cancel_replace(ViewBBCEditor *editor) {
    if (!editor || !editor->replace_active) return;
    editor->replace_active = 0;
    editor->mode = VIEWBBC_MODE_COMMAND;
    set_status(editor, "Replace cancelled");
}
