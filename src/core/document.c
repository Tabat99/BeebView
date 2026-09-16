#include "viewbbc/document.h"

#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int checked_add_size(size_t a, size_t b, size_t *out) {
    if (!out || a > SIZE_MAX - b) return 0;
    *out = a + b;
    return 1;
}

static int checked_mul_size(size_t a, size_t b, size_t *out) {
    if (!out || (a != 0 && b > SIZE_MAX / a)) return 0;
    *out = a * b;
    return 1;
}

static int document_can_grow(const ViewBBCDocument *doc, size_t amount) {
    if (!doc) return 0;
    return amount <= doc->workspace_limit && doc->used_bytes <= doc->workspace_limit - amount;
}

static int reserve_line(ViewBBCLine *line, size_t needed) {
    if (!line) return 0;
    if (needed <= line->capacity) return 1;

    size_t capacity = line->capacity ? line->capacity : 32u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < needed) return 0;

    uint8_t *data = realloc(line->data, capacity);
    if (!data) return 0;
    line->data = data;
    line->capacity = capacity;
    return 1;
}

static int reserve_lines(ViewBBCDocument *doc, size_t needed) {
    if (!doc) return 0;
    if (needed <= doc->line_capacity) return 1;

    size_t capacity = doc->line_capacity ? doc->line_capacity : 8u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = needed;
            break;
        }
        capacity *= 2u;
    }
    if (capacity < needed) return 0;

    size_t allocation_size;
    if (!checked_mul_size(capacity, sizeof(*doc->lines), &allocation_size)) return 0;
    ViewBBCLine *lines = realloc(doc->lines, allocation_size);
    if (!lines) return 0;
    doc->lines = lines;
    doc->line_capacity = capacity;
    return 1;
}

int viewbbc_document_init(ViewBBCDocument *doc) {
    return viewbbc_document_init_with_limit(doc, VIEWBBC_DEFAULT_WORKSPACE_LIMIT);
}

int viewbbc_document_init_with_limit(ViewBBCDocument *doc, size_t workspace_limit) {
    if (!doc || workspace_limit == 0) return 0;
    *doc = (ViewBBCDocument){0};
    doc->workspace_limit = workspace_limit;
    if (!reserve_lines(doc, 1)) return 0;
    doc->lines[0] = (ViewBBCLine){0};
    doc->line_count = 1;
    return 1;
}

void viewbbc_document_destroy(ViewBBCDocument *doc) {
    if (!doc) return;
    for (size_t i = 0; i < doc->line_count; ++i) free(doc->lines[i].data);
    free(doc->lines);
    *doc = (ViewBBCDocument){0};
}

void viewbbc_document_clear(ViewBBCDocument *doc) {
    if (!doc) return;
    for (size_t i = 0; i < doc->line_count; ++i) {
        free(doc->lines[i].data);
        doc->lines[i] = (ViewBBCLine){0};
    }
    if (doc->line_capacity == 0 && !reserve_lines(doc, 1)) return;
    doc->lines[0] = (ViewBBCLine){0};
    doc->line_count = 1;
    doc->used_bytes = 0;
}

size_t viewbbc_document_line_count(const ViewBBCDocument *doc) {
    return doc ? doc->line_count : 0;
}

size_t viewbbc_document_line_length(const ViewBBCDocument *doc, size_t line) {
    if (!doc || line >= doc->line_count) return 0;
    return doc->lines[line].length;
}

uint8_t viewbbc_document_char_at(const ViewBBCDocument *doc, size_t line, size_t column) {
    if (!doc || line >= doc->line_count || column >= doc->lines[line].length) return ' ';
    return doc->lines[line].data[column];
}

const char *viewbbc_document_command_at(const ViewBBCDocument *doc, size_t line) {
    if (!doc || line >= doc->line_count) return "";
    return doc->lines[line].command;
}

int viewbbc_document_set_command(ViewBBCDocument *doc, size_t line, const char command[2]) {
    if (!doc || line >= doc->line_count || !command ||
        !isalpha((unsigned char)command[0]) || !isalpha((unsigned char)command[1])) return 0;
    doc->lines[line].command[0] = (char)toupper((unsigned char)command[0]);
    doc->lines[line].command[1] = (char)toupper((unsigned char)command[1]);
    doc->lines[line].command[2] = '\0';
    return 1;
}

int viewbbc_document_delete_command(ViewBBCDocument *doc, size_t line) {
    if (!doc || line >= doc->line_count || doc->lines[line].command[0] == '\0') return 0;
    doc->lines[line].command[0] = '\0';
    doc->lines[line].command[1] = '\0';
    doc->lines[line].command[2] = '\0';
    return 1;
}

size_t viewbbc_document_bytes_used(const ViewBBCDocument *doc) {
    return doc ? doc->used_bytes : 0;
}

size_t viewbbc_document_bytes_free(const ViewBBCDocument *doc) {
    if (!doc || doc->used_bytes >= doc->workspace_limit) return 0;
    return doc->workspace_limit - doc->used_bytes;
}

size_t viewbbc_document_workspace_limit(const ViewBBCDocument *doc) {
    return doc ? doc->workspace_limit : 0;
}

int viewbbc_document_set_workspace_limit(ViewBBCDocument *doc, size_t workspace_limit) {
    if (!doc || workspace_limit == 0 || workspace_limit < doc->used_bytes) return 0;
    doc->workspace_limit = workspace_limit;
    return 1;
}

int viewbbc_workspace_parse_size(const char *text, size_t *bytes_out) {
    if (!text || !bytes_out) return 0;
    while (*text && isspace((unsigned char)*text)) ++text;
    if (!isdigit((unsigned char)*text)) return 0;
    size_t value = 0;
    while (isdigit((unsigned char)*text)) {
        unsigned digit = (unsigned)(*text - '0');
        if (value > (SIZE_MAX - digit) / 10u) return 0;
        value = value * 10u + digit;
        ++text;
    }
    while (*text && isspace((unsigned char)*text)) ++text;
    size_t multiplier = 1u;
    if (*text) {
        if ((text[0] == 'K' || text[0] == 'k') && (text[1] == 'B' || text[1] == 'b')) {
            multiplier = 1024u; text += 2;
        } else if ((text[0] == 'M' || text[0] == 'm') && (text[1] == 'B' || text[1] == 'b')) {
            multiplier = 1024u * 1024u; text += 2;
        } else return 0;
        while (*text && isspace((unsigned char)*text)) ++text;
        if (*text) return 0;
    }
    if (value > SIZE_MAX / multiplier) return 0;
    *bytes_out = value * multiplier;
    return 1;
}

void viewbbc_workspace_format_size(size_t bytes, char *out, size_t out_size) {
    if (!out || out_size == 0) return;
    if (bytes >= 1024u * 1024u && bytes % (1024u * 1024u) == 0)
        (void)snprintf(out, out_size, "%zu MB", bytes / (1024u * 1024u));
    else if (bytes >= 1024u && bytes % 1024u == 0)
        (void)snprintf(out, out_size, "%zu KB", bytes / 1024u);
    else
        (void)snprintf(out, out_size, "%zu bytes", bytes);
}

int viewbbc_document_insert_char(ViewBBCDocument *doc, size_t line_index, size_t column, uint8_t ch) {
    if (!doc || line_index >= doc->line_count || !document_can_grow(doc, 1)) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (column > line->length) column = line->length;

    size_t needed;
    if (!checked_add_size(line->length, 1, &needed) || !reserve_line(line, needed)) return 0;
    memmove(line->data + column + 1, line->data + column, line->length - column);
    line->data[column] = ch;
    line->length = needed;
    doc->used_bytes++;
    return 1;
}

int viewbbc_document_overwrite_char(ViewBBCDocument *doc, size_t line_index, size_t column, uint8_t ch) {
    if (!doc || line_index >= doc->line_count) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (column < line->length) {
        line->data[column] = ch;
        return 1;
    }
    return viewbbc_document_insert_char(doc, line_index, line->length, ch);
}

int viewbbc_document_pad_line_to(ViewBBCDocument *doc, size_t line_index, size_t length) {
    if (!doc || line_index >= doc->line_count) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (length <= line->length) return 1;

    size_t growth = length - line->length;
    if (!document_can_grow(doc, growth)) return 0;
    if (!reserve_line(line, length)) return 0;

    memset(line->data + line->length, ' ', growth);
    line->length = length;
    doc->used_bytes += growth;
    return 1;
}

int viewbbc_document_blank_char(ViewBBCDocument *doc, size_t line_index, size_t column) {
    if (!doc || line_index >= doc->line_count) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (column >= line->length) return 0;
    line->data[column] = ' ';
    return 1;
}

int viewbbc_document_delete_to_end(ViewBBCDocument *doc, size_t line_index, size_t column) {
    if (!doc || line_index >= doc->line_count) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (column >= line->length) return 1;
    size_t removed = line->length - column;
    line->length = column;
    doc->used_bytes -= removed;
    return 1;
}

int viewbbc_document_delete_char(ViewBBCDocument *doc, size_t line_index, size_t column) {
    if (!doc || line_index >= doc->line_count) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (column >= line->length) return 0;
    memmove(line->data + column, line->data + column + 1, line->length - column - 1);
    line->length--;
    doc->used_bytes--;
    return 1;
}

int viewbbc_document_delete_to_char(ViewBBCDocument *doc, size_t line_index, size_t column,
                                    uint8_t target, size_t *removed_out) {
    if (removed_out) *removed_out = 0;
    if (!doc || line_index >= doc->line_count) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (column >= line->length) return 0;
    size_t end = column;
    while (end < line->length && line->data[end] != target) ++end;
    if (end >= line->length) return 0;
    do { ++end; } while (end < line->length && line->data[end] == target);
    size_t removed = end - column;
    memmove(line->data + column, line->data + end, line->length - end);
    line->length -= removed;
    doc->used_bytes -= removed;
    if (removed_out) *removed_out = removed;
    return 1;
}

int viewbbc_document_split_line(ViewBBCDocument *doc, size_t line_index, size_t column) {
    if (!doc || line_index >= doc->line_count || !document_can_grow(doc, 1)) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (column > line->length) column = line->length;

    size_t new_count;
    if (!checked_add_size(doc->line_count, 1, &new_count) || !reserve_lines(doc, new_count)) return 0;

    line = &doc->lines[line_index];
    ViewBBCLine new_line = {0};
    size_t tail = line->length - column;
    if (tail && !reserve_line(&new_line, tail)) return 0;
    if (tail) memcpy(new_line.data, line->data + column, tail);
    new_line.length = tail;
    line->length = column;

    memmove(&doc->lines[line_index + 2],
            &doc->lines[line_index + 1],
            (doc->line_count - line_index - 1) * sizeof(*doc->lines));
    doc->lines[line_index + 1] = new_line;
    doc->line_count = new_count;
    doc->used_bytes++;
    return 1;
}

int viewbbc_document_join_with_previous(ViewBBCDocument *doc, size_t line_index) {
    if (!doc || line_index == 0 || line_index >= doc->line_count) return 0;
    ViewBBCLine *prev = &doc->lines[line_index - 1];
    ViewBBCLine *line = &doc->lines[line_index];

    size_t combined;
    if (!checked_add_size(prev->length, line->length, &combined) || !reserve_line(prev, combined)) return 0;
    if (line->length) memcpy(prev->data + prev->length, line->data, line->length);
    prev->length = combined;
    free(line->data);
    memmove(&doc->lines[line_index],
            &doc->lines[line_index + 1],
            (doc->line_count - line_index - 1) * sizeof(*doc->lines));
    doc->line_count--;
    doc->used_bytes--; /* one logical CR separator removed */
    return 1;
}

int viewbbc_document_join_with_next(ViewBBCDocument *doc, size_t line_index) {
    if (!doc || line_index + 1 >= doc->line_count) return 0;
    return viewbbc_document_join_with_previous(doc, line_index + 1);
}

int viewbbc_document_insert_line(ViewBBCDocument *doc, size_t line_index) {
    if (!doc || line_index > doc->line_count || !document_can_grow(doc, 1)) return 0;
    size_t new_count;
    if (!checked_add_size(doc->line_count, 1, &new_count) || !reserve_lines(doc, new_count)) return 0;
    memmove(&doc->lines[line_index + 1],
            &doc->lines[line_index],
            (doc->line_count - line_index) * sizeof(*doc->lines));
    doc->lines[line_index] = (ViewBBCLine){0};
    doc->line_count = new_count;
    doc->used_bytes++;
    return 1;
}

int viewbbc_document_delete_line(ViewBBCDocument *doc, size_t line_index) {
    if (!doc || line_index >= doc->line_count) return 0;
    if (doc->line_count == 1) {
        doc->used_bytes -= doc->lines[0].length;
        doc->lines[0].length = 0;
        return 1;
    }

    size_t removed = doc->lines[line_index].length + 1u; /* line bytes + one CR */
    free(doc->lines[line_index].data);
    memmove(&doc->lines[line_index],
            &doc->lines[line_index + 1],
            (doc->line_count - line_index - 1) * sizeof(*doc->lines));
    doc->line_count--;
    doc->used_bytes -= removed;
    return 1;
}

int viewbbc_document_load_bytes(ViewBBCDocument *doc, const uint8_t *data, size_t length) {
    if (!doc || (length != 0 && !data)) return 0;

    ViewBBCDocument temp;
    if (!viewbbc_document_init_with_limit(&temp, doc->workspace_limit)) return 0;

    for (size_t i = 0; i < length; ++i) {
        uint8_t ch = data[i];
        if (ch == '\r' || ch == '\n') {
            if (ch == '\r' && i + 1 < length && data[i + 1] == '\n') i++;
            if (!viewbbc_document_insert_line(&temp, temp.line_count)) {
                viewbbc_document_destroy(&temp);
                return 0;
            }
            continue;
        }

        size_t line = temp.line_count - 1;
        if (!viewbbc_document_insert_char(&temp, line, temp.lines[line].length, ch)) {
            viewbbc_document_destroy(&temp);
            return 0;
        }
    }

    /* BeebView stores VIEW edit commands as XX<TAB> at the start of a line.
       Keep the command in the three-column margin while exposing only text to editing. */
    for (size_t line = 0; line < temp.line_count; ++line) {
        ViewBBCLine *src = &temp.lines[line];
        if (src->length >= 3u && isalpha((unsigned char)src->data[0]) &&
            isalpha((unsigned char)src->data[1]) && src->data[2] == '\t') {
            char command[2] = {(char)src->data[0], (char)src->data[1]};
            (void)viewbbc_document_set_command(&temp, line, command);
            memmove(src->data, src->data + 3u, src->length - 3u);
            src->length -= 3u;
            temp.used_bytes -= 3u;
        }
    }
    viewbbc_document_destroy(doc);
    *doc = temp;
    return 1;
}


static int document_export_bytes(const ViewBBCDocument *doc, uint8_t **data_out, size_t *length_out) {
    if (!doc || !data_out || !length_out) return 0;
    *data_out = NULL;
    *length_out = 0;
    size_t length = doc->used_bytes;
    uint8_t *data = malloc(length ? length : 1u);
    if (!data) return 0;
    size_t w = 0;
    for (size_t i = 0; i < doc->line_count; ++i) {
        const ViewBBCLine *line = &doc->lines[i];
        if (line->length) {
            if (w > length || line->length > length - w) { free(data); return 0; }
            memcpy(data + w, line->data, line->length);
            w += line->length;
        }
        if (i + 1u < doc->line_count) {
            if (w >= length) { free(data); return 0; }
            data[w++] = '\r';
        }
    }
    if (w != length) { free(data); return 0; }
    *data_out = data;
    *length_out = length;
    return 1;
}

static int document_logical_offset(const ViewBBCDocument *doc,
                                   size_t line, size_t column,
                                   size_t *offset_out) {
    if (!doc || !offset_out || line >= doc->line_count) return 0;
    size_t offset = 0;
    for (size_t i = 0; i < line; ++i) {
        if (!checked_add_size(offset, doc->lines[i].length, &offset) ||
            !checked_add_size(offset, 1u, &offset)) return 0;
    }
    if (column > doc->lines[line].length) column = doc->lines[line].length;
    if (!checked_add_size(offset, column, &offset)) return 0;
    *offset_out = offset;
    return 1;
}

size_t viewbbc_document_count_words(const ViewBBCDocument *doc) {
    if (!doc) return 0;
    size_t count = 0;
    int in_word = 0;
    for (size_t i = 0; i < doc->line_count; ++i) {
        const ViewBBCLine *line = &doc->lines[i];
        for (size_t j = 0; j < line->length; ++j) {
            uint8_t ch = line->data[j];
            int separator = ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
            if (separator) in_word = 0;
            else if (!in_word) { count++; in_word = 1; }
        }
        in_word = 0;
    }
    return count;
}

int viewbbc_document_find(const ViewBBCDocument *doc,
                          const uint8_t *needle, size_t needle_length,
                          size_t start_line, size_t start_column,
                          size_t *line_out, size_t *column_out) {
    if (!doc || !needle || needle_length == 0 || !line_out || !column_out ||
        start_line >= doc->line_count) return 0;
    for (size_t line_index = start_line; line_index < doc->line_count; ++line_index) {
        const ViewBBCLine *line = &doc->lines[line_index];
        size_t start = line_index == start_line ? start_column : 0u;
        if (start > line->length) start = line->length;
        if (needle_length > line->length || start > line->length - needle_length) continue;
        for (size_t column = start; column <= line->length - needle_length; ++column) {
            if (memcmp(line->data + column, needle, needle_length) == 0) {
                *line_out = line_index;
                *column_out = column;
                return 1;
            }
        }
    }
    return 0;
}

int viewbbc_document_insert_bytes_at(ViewBBCDocument *doc,
                                     size_t line, size_t column,
                                     const uint8_t *data, size_t length) {
    if (!doc || line >= doc->line_count || (length && !data)) return 0;
    if (length == 0) return 1;

    ViewBBCDocument insert_doc;
    if (!viewbbc_document_init_with_limit(&insert_doc, doc->workspace_limit)) return 0;
    if (!viewbbc_document_load_bytes(&insert_doc, data, length)) {
        viewbbc_document_destroy(&insert_doc);
        return 0;
    }
    if (insert_doc.used_bytes > doc->workspace_limit ||
        doc->used_bytes > doc->workspace_limit - insert_doc.used_bytes) {
        viewbbc_document_destroy(&insert_doc);
        return 0;
    }

    uint8_t *current = NULL, *inserted = NULL;
    size_t current_length = 0, inserted_length = 0;
    size_t offset = 0;
    if (!document_logical_offset(doc, line, column, &offset) ||
        !document_export_bytes(doc, &current, &current_length) ||
        !document_export_bytes(&insert_doc, &inserted, &inserted_length)) {
        free(current); free(inserted); viewbbc_document_destroy(&insert_doc); return 0;
    }
    viewbbc_document_destroy(&insert_doc);

    size_t total;
    if (!checked_add_size(current_length, inserted_length, &total) || total > doc->workspace_limit) {
        free(current); free(inserted); return 0;
    }
    uint8_t *combined = malloc(total ? total : 1u);
    if (!combined) { free(current); free(inserted); return 0; }
    if (offset) memcpy(combined, current, offset);
    if (inserted_length) memcpy(combined + offset, inserted, inserted_length);
    if (current_length > offset)
        memcpy(combined + offset + inserted_length, current + offset, current_length - offset);

    int ok = viewbbc_document_load_bytes(doc, combined, total);
    free(combined); free(current); free(inserted);
    return ok;
}

int viewbbc_document_replace_at(ViewBBCDocument *doc,
                                size_t line_index, size_t column, size_t old_length,
                                const uint8_t *replacement, size_t replacement_length) {
    if (!doc || line_index >= doc->line_count ||
        (replacement_length && !replacement)) return 0;
    ViewBBCLine *line = &doc->lines[line_index];
    if (column > line->length || old_length > line->length - column) return 0;

    size_t new_length = line->length;
    if (replacement_length >= old_length) {
        size_t growth = replacement_length - old_length;
        if (!document_can_grow(doc, growth) || !checked_add_size(new_length, growth, &new_length)) return 0;
    } else {
        new_length -= old_length - replacement_length;
    }
    if (!reserve_line(line, new_length)) return 0;

    size_t tail_start = column + old_length;
    size_t tail_length = line->length - tail_start;
    if (replacement_length != old_length) {
        memmove(line->data + column + replacement_length,
                line->data + tail_start, tail_length);
    }
    if (replacement_length) memcpy(line->data + column, replacement, replacement_length);
    if (new_length >= line->length) doc->used_bytes += new_length - line->length;
    else doc->used_bytes -= line->length - new_length;
    line->length = new_length;
    return 1;
}

int viewbbc_document_replace_all(ViewBBCDocument *doc,
                                 const uint8_t *needle, size_t needle_length,
                                 const uint8_t *replacement, size_t replacement_length,
                                 size_t *replacement_count) {
    if (!doc || !needle || needle_length == 0 ||
        (replacement_length && !replacement) || !replacement_count) return 0;
    *replacement_count = 0;

    uint8_t *source = NULL;
    size_t source_length = 0;
    if (!document_export_bytes(doc, &source, &source_length)) return 0;

    size_t count = 0;
    for (size_t i = 0; i + needle_length <= source_length;) {
        if (memcmp(source + i, needle, needle_length) == 0) {
            count++;
            i += needle_length;
        } else i++;
    }
    if (count == 0) { free(source); return 1; }

    size_t new_length = source_length;
    if (replacement_length >= needle_length) {
        size_t growth = replacement_length - needle_length;
        if (growth && count > (SIZE_MAX - new_length) / growth) { free(source); return 0; }
        new_length += count * growth;
    } else {
        size_t shrink = needle_length - replacement_length;
        if (count > new_length / shrink && shrink != 0) { free(source); return 0; }
        new_length -= count * shrink;
    }
    if (new_length > doc->workspace_limit) { free(source); return 0; }

    uint8_t *result = malloc(new_length ? new_length : 1u);
    if (!result) { free(source); return 0; }
    size_t r = 0, w = 0;
    while (r < source_length) {
        if (r + needle_length <= source_length &&
            memcmp(source + r, needle, needle_length) == 0) {
            if (replacement_length) memcpy(result + w, replacement, replacement_length);
            w += replacement_length;
            r += needle_length;
        } else result[w++] = source[r++];
    }
    if (w != new_length) { free(source); free(result); return 0; }
    int ok = viewbbc_document_load_bytes(doc, result, new_length);
    free(source); free(result);
    if (!ok) return 0;
    *replacement_count = count;
    return 1;
}
