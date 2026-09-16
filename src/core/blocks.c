#include "viewbbc/blocks.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int checked_add(size_t a, size_t b, size_t *out) {
    if (!out || a > SIZE_MAX - b) return 0;
    *out = a + b;
    return 1;
}

static int position_to_offset(const ViewBBCDocument *doc,
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

static int offset_to_position(const ViewBBCDocument *doc,
                              size_t offset,
                              size_t *line_out, size_t *column_out) {
    if (!doc || !line_out || !column_out || offset > viewbbc_document_bytes_used(doc)) return 0;
    size_t lines = viewbbc_document_line_count(doc);
    size_t base = 0;
    for (size_t line = 0; line < lines; ++line) {
        size_t length = viewbbc_document_line_length(doc, line);
        if (offset <= base + length) {
            *line_out = line;
            *column_out = offset - base;
            return 1;
        }
        base += length;
        if (line + 1u < lines) base++;
    }
    *line_out = lines ? lines - 1u : 0u;
    *column_out = lines ? viewbbc_document_line_length(doc, lines - 1u) : 0u;
    return lines != 0;
}

static int export_document(const ViewBBCDocument *doc, uint8_t **data_out, size_t *length_out) {
    if (!doc || !data_out || !length_out) return 0;
    size_t length = viewbbc_document_bytes_used(doc);
    uint8_t *data = malloc(length ? length : 1u);
    if (!data) return 0;
    size_t w = 0;
    size_t lines = viewbbc_document_line_count(doc);
    for (size_t line = 0; line < lines; ++line) {
        size_t line_length = viewbbc_document_line_length(doc, line);
        for (size_t column = 0; column < line_length; ++column)
            data[w++] = viewbbc_document_char_at(doc, line, column);
        if (line + 1u < lines) data[w++] = '\r';
    }
    if (w != length) {
        free(data);
        return 0;
    }
    *data_out = data;
    *length_out = length;
    return 1;
}

static ViewBBCBlockResult block_offsets(const ViewBBCDocument *doc,
                                        const ViewBBCMarkers *markers,
                                        size_t *start_out, size_t *end_out) {
    size_t line1, column1, line2, column2;
    if (!viewbbc_markers_get(markers, 1u, &line1, &column1) ||
        !viewbbc_markers_get(markers, 2u, &line2, &column2))
        return VIEWBBC_BLOCK_MARKERS_UNSET;
    size_t start, end;
    if (!position_to_offset(doc, line1, column1, &start) ||
        !position_to_offset(doc, line2, column2, &end) || start >= end)
        return VIEWBBC_BLOCK_MARKERS_INVALID;
    *start_out = start;
    *end_out = end;
    return VIEWBBC_BLOCK_OK;
}

static void restore_markers_after_delete(ViewBBCMarkers *markers,
                                         const ViewBBCDocument *old_doc,
                                         const ViewBBCDocument *new_doc,
                                         size_t start, size_t end) {
    size_t removed = end - start;
    for (unsigned number = 1u; number <= VIEWBBC_MARKER_COUNT; ++number) {
        size_t line, column, offset;
        if (!viewbbc_markers_get(markers, number, &line, &column)) continue;
        if (!position_to_offset(old_doc, line, column, &offset)) {
            (void)viewbbc_markers_unset(markers, number);
            continue;
        }
        if (offset >= start && offset <= end) {
            (void)viewbbc_markers_unset(markers, number);
            continue;
        }
        if (offset > end) offset -= removed;
        if (!offset_to_position(new_doc, offset, &line, &column)) {
            (void)viewbbc_markers_unset(markers, number);
            continue;
        }
        (void)viewbbc_markers_set(markers, number, line, column);
    }
}

ViewBBCBlockResult viewbbc_block_delete(ViewBBCDocument *doc,
                                        ViewBBCMarkers *markers,
                                        size_t *cursor_line,
                                        size_t *cursor_column) {
    if (!doc || !markers || !cursor_line || !cursor_column) return VIEWBBC_BLOCK_MARKERS_INVALID;
    size_t start, end;
    ViewBBCBlockResult result = block_offsets(doc, markers, &start, &end);
    if (result != VIEWBBC_BLOCK_OK) return result;

    uint8_t *source = NULL;
    size_t source_length = 0;
    if (!export_document(doc, &source, &source_length)) return VIEWBBC_BLOCK_NO_MEMORY;
    size_t removed = end - start;
    size_t new_length = source_length - removed;
    uint8_t *combined = malloc(new_length ? new_length : 1u);
    if (!combined) { free(source); return VIEWBBC_BLOCK_NO_MEMORY; }
    if (start) memcpy(combined, source, start);
    if (source_length > end) memcpy(combined + start, source + end, source_length - end);

    ViewBBCDocument old = *doc;
    ViewBBCDocument rebuilt;
    if (!viewbbc_document_init_with_limit(&rebuilt, viewbbc_document_workspace_limit(doc))) {
        free(source); free(combined); return VIEWBBC_BLOCK_NO_MEMORY;
    }
    if (!viewbbc_document_load_bytes(&rebuilt, combined, new_length)) {
        viewbbc_document_destroy(&rebuilt); free(source); free(combined); return VIEWBBC_BLOCK_NO_MEMORY;
    }
    *doc = rebuilt;
    restore_markers_after_delete(markers, &old, doc, start, end);
    if (!offset_to_position(doc, start, cursor_line, cursor_column)) {
        *cursor_line = 0; *cursor_column = 0;
    }
    viewbbc_document_destroy(&old);
    free(source);
    free(combined);
    return VIEWBBC_BLOCK_OK;
}

ViewBBCBlockResult viewbbc_block_copy(ViewBBCDocument *doc,
                                      ViewBBCMarkers *markers,
                                      size_t destination_line,
                                      size_t destination_column,
                                      size_t *cursor_line,
                                      size_t *cursor_column) {
    if (!doc || !markers || !cursor_line || !cursor_column) return VIEWBBC_BLOCK_MARKERS_INVALID;
    size_t start, end, destination;
    ViewBBCBlockResult result = block_offsets(doc, markers, &start, &end);
    if (result != VIEWBBC_BLOCK_OK) return result;
    if (!position_to_offset(doc, destination_line, destination_column, &destination))
        return VIEWBBC_BLOCK_MARKERS_INVALID;
    if (destination > start && destination < end) return VIEWBBC_BLOCK_DESTINATION_INSIDE;

    uint8_t *source = NULL;
    size_t source_length = 0;
    if (!export_document(doc, &source, &source_length)) return VIEWBBC_BLOCK_NO_MEMORY;
    size_t block_length = end - start;
    size_t new_length = 0;
    if (!checked_add(source_length, block_length, &new_length) ||
        new_length > viewbbc_document_workspace_limit(doc)) {
        free(source);
        return VIEWBBC_BLOCK_NO_MEMORY;
    }
    uint8_t *combined = malloc(new_length ? new_length : 1u);
    if (!combined) { free(source); return VIEWBBC_BLOCK_NO_MEMORY; }

    if (destination) memcpy(combined, source, destination);
    memcpy(combined + destination, source + start, block_length);
    if (source_length > destination)
        memcpy(combined + destination + block_length, source + destination, source_length - destination);

    if (!viewbbc_document_load_bytes(doc, combined, new_length)) {
        free(source); free(combined); return VIEWBBC_BLOCK_NO_MEMORY;
    }

    /* Keep markers attached to the original block if insertion was before it. */
    size_t original_start = start;
    size_t original_end = end;
    if (destination <= start) {
        original_start += block_length;
        original_end += block_length;
    }
    size_t line, column;
    if (offset_to_position(doc, original_start, &line, &column))
        (void)viewbbc_markers_set(markers, 1u, line, column);
    if (offset_to_position(doc, original_end, &line, &column))
        (void)viewbbc_markers_set(markers, 2u, line, column);

    size_t after_copy = destination + block_length;
    if (!offset_to_position(doc, after_copy, cursor_line, cursor_column)) {
        *cursor_line = 0; *cursor_column = 0;
    }
    free(source);
    free(combined);
    return VIEWBBC_BLOCK_OK;
}

ViewBBCBlockResult viewbbc_block_move(ViewBBCDocument *doc,
                                      ViewBBCMarkers *markers,
                                      size_t destination_line,
                                      size_t destination_column,
                                      size_t *cursor_line,
                                      size_t *cursor_column) {
    if (!doc || !markers || !cursor_line || !cursor_column) return VIEWBBC_BLOCK_MARKERS_INVALID;
    size_t start, end, destination;
    ViewBBCBlockResult result = block_offsets(doc, markers, &start, &end);
    if (result != VIEWBBC_BLOCK_OK) return result;
    if (!position_to_offset(doc, destination_line, destination_column, &destination))
        return VIEWBBC_BLOCK_MARKERS_INVALID;
    if (destination > start && destination < end) return VIEWBBC_BLOCK_DESTINATION_INSIDE;

    uint8_t *source = NULL;
    size_t source_length = 0;
    if (!export_document(doc, &source, &source_length)) return VIEWBBC_BLOCK_NO_MEMORY;
    size_t block_length = end - start;
    uint8_t *combined = malloc(source_length ? source_length : 1u);
    if (!combined) { free(source); return VIEWBBC_BLOCK_NO_MEMORY; }

    size_t adjusted_destination = destination;
    if (destination >= end) adjusted_destination -= block_length;

    size_t w = 0;
    if (adjusted_destination <= start) {
        if (adjusted_destination) { memcpy(combined + w, source, adjusted_destination); w += adjusted_destination; }
        memcpy(combined + w, source + start, block_length); w += block_length;
        if (start > adjusted_destination) { memcpy(combined + w, source + adjusted_destination, start - adjusted_destination); w += start - adjusted_destination; }
        if (source_length > end) { memcpy(combined + w, source + end, source_length - end); w += source_length - end; }
    } else {
        if (start) { memcpy(combined + w, source, start); w += start; }
        if (destination > end) { memcpy(combined + w, source + end, destination - end); w += destination - end; }
        memcpy(combined + w, source + start, block_length); w += block_length;
        if (source_length > destination) { memcpy(combined + w, source + destination, source_length - destination); w += source_length - destination; }
    }
    if (w != source_length) { free(source); free(combined); return VIEWBBC_BLOCK_MARKERS_INVALID; }

    if (!viewbbc_document_load_bytes(doc, combined, source_length)) {
        free(source); free(combined); return VIEWBBC_BLOCK_NO_MEMORY;
    }

    size_t moved_start = adjusted_destination;
    size_t moved_end = moved_start + block_length;
    size_t line, column;
    if (offset_to_position(doc, moved_start, &line, &column))
        (void)viewbbc_markers_set(markers, 1u, line, column);
    if (offset_to_position(doc, moved_end, &line, &column))
        (void)viewbbc_markers_set(markers, 2u, line, column);
    if (!offset_to_position(doc, moved_start, cursor_line, cursor_column)) {
        *cursor_line = 0; *cursor_column = 0;
    }

    free(source);
    free(combined);
    return VIEWBBC_BLOCK_OK;
}
