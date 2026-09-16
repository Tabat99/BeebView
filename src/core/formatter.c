#include "viewbbc/formatter.h"
#include "viewbbc/ruler.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t length;
} Word;

typedef struct {
    uint8_t *data;
    size_t length;
    size_t capacity;
} ByteBuffer;

static int checked_add(size_t a, size_t b, size_t *out) {
    if (!out || a > SIZE_MAX - b) return 0;
    *out = a + b;
    return 1;
}

static int buffer_reserve(ByteBuffer *buffer, size_t extra) {
    size_t needed = 0;
    if (!buffer || !checked_add(buffer->length, extra, &needed)) return 0;
    if (needed <= buffer->capacity) return 1;
    size_t capacity = buffer->capacity ? buffer->capacity : 128u;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2u) { capacity = needed; break; }
        capacity *= 2u;
    }
    uint8_t *grown = realloc(buffer->data, capacity);
    if (!grown) return 0;
    buffer->data = grown;
    buffer->capacity = capacity;
    return 1;
}

static int buffer_append(ByteBuffer *buffer, const uint8_t *data, size_t length) {
    if (length == 0) return 1;
    if (!data || !buffer_reserve(buffer, length)) return 0;
    memcpy(buffer->data + buffer->length, data, length);
    buffer->length += length;
    return 1;
}

static int buffer_put(ByteBuffer *buffer, uint8_t ch) {
    return buffer_append(buffer, &ch, 1u);
}

static int boundary_line(const ViewBBCDocument *doc, size_t line) {
    size_t length = viewbbc_document_line_length(doc, line);
    if (length == 0) return 1;
    uint8_t ch = viewbbc_document_char_at(doc, line, 0u);
    return ch == ' ' || ch == '\t';
}

static int collect_words(const ViewBBCDocument *doc, size_t first, size_t last,
                         Word **words_out, size_t *count_out) {
    if (!words_out || !count_out) return 0;
    *words_out = NULL;
    *count_out = 0;
    size_t capacity = 0;
    Word *words = NULL;

    for (size_t line = first; line < last; ++line) {
        const ViewBBCLine *src = &doc->lines[line];
        size_t pos = 0;
        while (pos < src->length) {
            while (pos < src->length && (src->data[pos] == ' ' || src->data[pos] == '\t')) ++pos;
            if (pos >= src->length) break;
            size_t start = pos;
            while (pos < src->length && src->data[pos] != ' ' && src->data[pos] != '\t') ++pos;
            if (*count_out == capacity) {
                size_t next = capacity ? capacity * 2u : 32u;
                if (next < capacity || next > SIZE_MAX / sizeof(*words)) { free(words); return 0; }
                Word *grown = realloc(words, next * sizeof(*words));
                if (!grown) { free(words); return 0; }
                words = grown;
                capacity = next;
            }
            words[*count_out].data = src->data + start;
            words[*count_out].length = pos - start;
            ++*count_out;
        }
    }
    *words_out = words;
    return 1;
}

static int append_formatted_line(ByteBuffer *out, const Word *words,
                                 size_t first, size_t count, int justify,
                                 int final_line) {
    if (count == 0) return 1;
    size_t chars = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!checked_add(chars, words[first + i].length, &chars)) return 0;
    }
    size_t gaps = count - 1u;
    size_t spaces = gaps;
    if (justify && !final_line && gaps > 0 && chars + gaps < VIEWBBC_RULER_WIDTH)
        spaces = VIEWBBC_RULER_WIDTH - chars;

    size_t base = gaps ? spaces / gaps : 0u;
    size_t extra = gaps ? spaces % gaps : 0u;
    for (size_t i = 0; i < count; ++i) {
        if (!buffer_append(out, words[first + i].data, words[first + i].length)) return 0;
        if (i + 1u < count) {
            size_t gap = base + (i < extra ? 1u : 0u);
            for (size_t n = 0; n < gap; ++n) if (!buffer_put(out, ' ')) return 0;
        }
    }
    return 1;
}

static int format_words(const Word *words, size_t count, int justify,
                        ByteBuffer *out, size_t *line_count_out) {
    *line_count_out = 0;
    size_t first = 0;
    while (first < count) {
        size_t line_chars = 0;
        size_t n = 0;
        while (first + n < count) {
            size_t word_len = words[first + n].length;
            size_t needed = word_len;
            if (n > 0 && !checked_add(needed, 1u, &needed)) return 0;
            if (n > 0 && (line_chars > VIEWBBC_RULER_WIDTH || needed > VIEWBBC_RULER_WIDTH - line_chars)) break;
            if (n == 0 && word_len > VIEWBBC_RULER_WIDTH) {
                n = 1u;
                line_chars = word_len;
                break;
            }
            line_chars += needed;
            ++n;
        }
        if (n == 0) return 0;
        int final_line = first + n == count;
        if (!append_formatted_line(out, words, first, n, justify, final_line)) return 0;
        ++*line_count_out;
        first += n;
        if (!final_line && !buffer_put(out, '\r')) return 0;
    }
    return 1;
}

static int append_document_lines(ByteBuffer *out, const ViewBBCDocument *doc,
                                 size_t first, size_t last, int leading_cr) {
    for (size_t line = first; line < last; ++line) {
        if ((leading_cr || line > first) && !buffer_put(out, '\r')) return 0;
        if (!buffer_append(out, doc->lines[line].data, doc->lines[line].length)) return 0;
    }
    return 1;
}

static void update_markers(ViewBBCMarkers *markers, size_t first, size_t old_last,
                           size_t new_line_count) {
    size_t old_count = old_last - first;
    for (unsigned number = 1u; number <= VIEWBBC_MARKER_COUNT; ++number) {
        size_t line = 0, column = 0;
        if (!viewbbc_markers_get(markers, number, &line, &column)) continue;
        if (line >= first && line < old_last) {
            (void)viewbbc_markers_unset(markers, number);
        } else if (line >= old_last) {
            if (new_line_count >= old_count) line += new_line_count - old_count;
            else line -= old_count - new_line_count;
            (void)viewbbc_markers_set(markers, number, line, column);
        }
    }
}

ViewBBCFormatResult viewbbc_format_block(ViewBBCDocument *doc,
                                         ViewBBCMarkers *markers,
                                         size_t start_line,
                                         int format_mode,
                                         int justify,
                                         size_t *cursor_line,
                                         size_t *cursor_column) {
    if (!doc || !markers || !cursor_line || !cursor_column) return VIEWBBC_FORMAT_NOTHING;
    if (!format_mode) return VIEWBBC_FORMAT_MODE_OFF;
    size_t line_count = viewbbc_document_line_count(doc);
    if (start_line >= line_count || boundary_line(doc, start_line)) return VIEWBBC_FORMAT_NOTHING;

    size_t end_line = start_line + 1u;
    while (end_line < line_count && !boundary_line(doc, end_line)) ++end_line;

    Word *words = NULL;
    size_t word_count = 0;
    if (!collect_words(doc, start_line, end_line, &words, &word_count)) return VIEWBBC_FORMAT_NO_MEMORY;
    if (word_count == 0) { free(words); return VIEWBBC_FORMAT_NOTHING; }

    ByteBuffer formatted = {0};
    size_t formatted_lines = 0;
    if (!format_words(words, word_count, justify, &formatted, &formatted_lines)) {
        free(words); free(formatted.data); return VIEWBBC_FORMAT_NO_MEMORY;
    }

    ByteBuffer rebuilt = {0};
    int ok = append_document_lines(&rebuilt, doc, 0u, start_line, 0);
    if (ok && start_line > 0u) ok = buffer_put(&rebuilt, '\r');
    if (ok) ok = buffer_append(&rebuilt, formatted.data, formatted.length);
    if (ok && end_line < line_count) ok = append_document_lines(&rebuilt, doc, end_line, line_count, 1);
    if (!ok || rebuilt.length > viewbbc_document_workspace_limit(doc)) {
        free(words); free(formatted.data); free(rebuilt.data); return VIEWBBC_FORMAT_NO_MEMORY;
    }

    if (!viewbbc_document_load_bytes(doc, rebuilt.data, rebuilt.length)) {
        free(words); free(formatted.data); free(rebuilt.data); return VIEWBBC_FORMAT_NO_MEMORY;
    }
    update_markers(markers, start_line, end_line, formatted_lines);
    *cursor_line = start_line;
    *cursor_column = 0u;

    free(words);
    free(formatted.data);
    free(rebuilt.data);
    return VIEWBBC_FORMAT_OK;
}


static int fmt_position_to_offset(const ViewBBCDocument *doc,
                                  size_t line, size_t column,
                                  size_t *offset_out) {
    if (!doc || !offset_out || line >= viewbbc_document_line_count(doc)) return 0;
    size_t offset = 0;
    for (size_t i = 0; i < line; ++i) {
        size_t length = viewbbc_document_line_length(doc, i);
        if (!checked_add(offset, length, &offset) || !checked_add(offset, 1u, &offset)) return 0;
    }
    size_t length = viewbbc_document_line_length(doc, line);
    if (column > length) column = length;
    if (!checked_add(offset, column, &offset)) return 0;
    *offset_out = offset;
    return 1;
}

static int fmt_offset_to_position(const ViewBBCDocument *doc, size_t offset,
                                  size_t *line_out, size_t *column_out) {
    if (!doc || !line_out || !column_out || offset > viewbbc_document_bytes_used(doc)) return 0;
    size_t base = 0;
    size_t lines = viewbbc_document_line_count(doc);
    for (size_t line = 0; line < lines; ++line) {
        size_t length = viewbbc_document_line_length(doc, line);
        if (offset <= base + length) {
            *line_out = line;
            *column_out = offset - base;
            return 1;
        }
        base += length;
        if (line + 1u < lines) ++base;
    }
    if (!lines) return 0;
    *line_out = lines - 1u;
    *column_out = viewbbc_document_line_length(doc, lines - 1u);
    return 1;
}

static int fmt_export_document(const ViewBBCDocument *doc, uint8_t **data_out, size_t *length_out) {
    if (!doc || !data_out || !length_out) return 0;
    size_t length = viewbbc_document_bytes_used(doc);
    uint8_t *data = malloc(length ? length : 1u);
    if (!data) return 0;
    size_t w = 0;
    size_t lines = viewbbc_document_line_count(doc);
    for (size_t line = 0; line < lines; ++line) {
        size_t n = viewbbc_document_line_length(doc, line);
        if (n) memcpy(data + w, doc->lines[line].data, n);
        w += n;
        if (line + 1u < lines) data[w++] = '\r';
    }
    if (w != length) { free(data); return 0; }
    *data_out = data;
    *length_out = length;
    return 1;
}

static ViewBBCFormatResult format_all_in_document(ViewBBCDocument *doc, int justify) {
    ViewBBCMarkers scratch;
    viewbbc_markers_clear(&scratch);
    size_t line = 0;
    while (line < viewbbc_document_line_count(doc)) {
        if (boundary_line(doc, line)) { ++line; continue; }
        size_t cursor_line = line, cursor_column = 0;
        ViewBBCFormatResult result = viewbbc_format_block(doc, &scratch, line, 1, justify,
                                                           &cursor_line, &cursor_column);
        if (result == VIEWBBC_FORMAT_NO_MEMORY) return result;
        if (result != VIEWBBC_FORMAT_OK) { ++line; continue; }
        line = cursor_line;
        while (line < viewbbc_document_line_count(doc) && !boundary_line(doc, line)) ++line;
    }
    return VIEWBBC_FORMAT_OK;
}

ViewBBCFormatResult viewbbc_format_global(ViewBBCDocument *doc,
                                          ViewBBCMarkers *markers,
                                          int has_range,
                                          unsigned first_marker,
                                          unsigned second_marker,
                                          int justify,
                                          size_t *cursor_line,
                                          size_t *cursor_column) {
    if (!doc || !markers || !cursor_line || !cursor_column) return VIEWBBC_FORMAT_NOTHING;

    size_t source_length = viewbbc_document_bytes_used(doc);
    size_t start = 0, end = source_length;
    if (has_range) {
        size_t sl, sc, el, ec;
        if (!viewbbc_markers_get(markers, first_marker, &sl, &sc) ||
            !viewbbc_markers_get(markers, second_marker, &el, &ec) ||
            !fmt_position_to_offset(doc, sl, sc, &start) ||
            !fmt_position_to_offset(doc, el, ec, &end) || start >= end)
            return VIEWBBC_FORMAT_NOTHING;
    }

    uint8_t *source = NULL;
    if (!fmt_export_document(doc, &source, &source_length)) return VIEWBBC_FORMAT_NO_MEMORY;

    ViewBBCDocument selected;
    if (!viewbbc_document_init_with_limit(&selected, viewbbc_document_workspace_limit(doc))) {
        free(source); return VIEWBBC_FORMAT_NO_MEMORY;
    }
    if (!viewbbc_document_load_bytes(&selected, source + start, end - start)) {
        viewbbc_document_destroy(&selected); free(source); return VIEWBBC_FORMAT_NO_MEMORY;
    }
    ViewBBCFormatResult result = format_all_in_document(&selected, justify);
    if (result != VIEWBBC_FORMAT_OK) {
        viewbbc_document_destroy(&selected); free(source); return result;
    }

    uint8_t *formatted = NULL;
    size_t formatted_length = 0;
    if (!fmt_export_document(&selected, &formatted, &formatted_length)) {
        viewbbc_document_destroy(&selected); free(source); return VIEWBBC_FORMAT_NO_MEMORY;
    }
    viewbbc_document_destroy(&selected);

    size_t suffix = source_length - end;
    size_t new_length = 0;
    if (!checked_add(start, formatted_length, &new_length) ||
        !checked_add(new_length, suffix, &new_length) ||
        new_length > viewbbc_document_workspace_limit(doc)) {
        free(formatted); free(source); return VIEWBBC_FORMAT_NO_MEMORY;
    }
    uint8_t *rebuilt = malloc(new_length ? new_length : 1u);
    if (!rebuilt) { free(formatted); free(source); return VIEWBBC_FORMAT_NO_MEMORY; }
    if (start) memcpy(rebuilt, source, start);
    if (formatted_length) memcpy(rebuilt + start, formatted, formatted_length);
    if (suffix) memcpy(rebuilt + start + formatted_length, source + end, suffix);

    size_t marker_offsets[VIEWBBC_MARKER_COUNT] = {0};
    int marker_set[VIEWBBC_MARKER_COUNT] = {0};
    for (unsigned n = 1u; n <= VIEWBBC_MARKER_COUNT; ++n) {
        size_t ml, mc;
        if (viewbbc_markers_get(markers, n, &ml, &mc) &&
            fmt_position_to_offset(doc, ml, mc, &marker_offsets[n - 1u])) marker_set[n - 1u] = 1;
    }

    if (!viewbbc_document_load_bytes(doc, rebuilt, new_length)) {
        free(rebuilt); free(formatted); free(source); return VIEWBBC_FORMAT_NO_MEMORY;
    }

    size_t new_end = start + formatted_length;
    for (unsigned n = 1u; n <= VIEWBBC_MARKER_COUNT; ++n) {
        if (!marker_set[n - 1u]) continue;
        size_t old = marker_offsets[n - 1u], mapped;
        if (old < start) mapped = old;
        else if (old == start) mapped = start;
        else if (old == end) mapped = new_end;
        else if (old > end) mapped = new_end + (old - end);
        else { (void)viewbbc_markers_unset(markers, n); continue; }
        size_t ml, mc;
        if (fmt_offset_to_position(doc, mapped, &ml, &mc)) (void)viewbbc_markers_set(markers, n, ml, mc);
        else (void)viewbbc_markers_unset(markers, n);
    }

    if (!fmt_offset_to_position(doc, start, cursor_line, cursor_column)) {
        *cursor_line = 0; *cursor_column = 0;
    }
    free(rebuilt); free(formatted); free(source);
    return VIEWBBC_FORMAT_OK;
}
