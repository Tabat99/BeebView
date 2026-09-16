#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "viewbbc/file_io.h"
#include "atomic_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

int viewbbc_file_exists(const char *path) {
    if (!path || !*path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int viewbbc_file_read_all(const char *path,
                          uint8_t **data_out,
                          size_t *length_out,
                          size_t maximum_bytes) {
    if (!path || !*path || !data_out || !length_out || maximum_bytes == 0) return 0;
    *data_out = NULL;
    *length_out = 0;

    FILE *f = fopen(path, "rb");
    if (!f) return 0;

    size_t capacity = 4096u;
    if (capacity > maximum_bytes) capacity = maximum_bytes;
    uint8_t *buffer = malloc(capacity ? capacity : 1u);
    if (!buffer) {
        fclose(f);
        return 0;
    }

    size_t length = 0;
    int ok = 1;
    for (;;) {
        if (length == capacity) {
            if (capacity >= maximum_bytes) {
                int c = fgetc(f);
                if (c != EOF) ok = 0;
                break;
            }
            size_t next = capacity <= maximum_bytes / 2u ? capacity * 2u : maximum_bytes;
            if (next <= capacity) {
                ok = 0;
                break;
            }
            uint8_t *grown = realloc(buffer, next);
            if (!grown) {
                ok = 0;
                break;
            }
            buffer = grown;
            capacity = next;
        }

        size_t got = fread(buffer + length, 1, capacity - length, f);
        length += got;
        if (got == 0) {
            if (ferror(f)) ok = 0;
            break;
        }
    }

    fclose(f);
    if (!ok) {
        free(buffer);
        return 0;
    }

    *data_out = buffer;
    *length_out = length;
    return 1;
}

int viewbbc_file_load_document(const char *path, ViewBBCDocument *document) {
    if (!path || !document) return 0;

    size_t max_read = viewbbc_document_workspace_limit(document);
    if (max_read > VIEWBBC_HOST_READ_LIMIT / 2u) max_read = VIEWBBC_HOST_READ_LIMIT / 2u;
    if (max_read > (SIZE_MAX - 1u) / 2u) return 0;
    max_read = max_read * 2u + 1u; /* permits CRLF input before normalisation */
    if (max_read > VIEWBBC_HOST_READ_LIMIT) max_read = VIEWBBC_HOST_READ_LIMIT;

    uint8_t *data = NULL;
    size_t length = 0;
    if (!viewbbc_file_read_all(path, &data, &length, max_read)) return 0;
    int ok = viewbbc_document_load_bytes(document, data, length);
    free(data);
    return ok;
}

int viewbbc_file_save_document(const char *path, const ViewBBCDocument *document) {
    if (!path || !*path || !document) return 0;
    char tmp[4096]; FILE *f=viewbbc_atomic_open(path,tmp,sizeof(tmp)); if(!f)return 0;
    int ok = 1;
    size_t lines = viewbbc_document_line_count(document);
    for (size_t i = 0; i < lines && ok; ++i) {
        const ViewBBCLine *line = &document->lines[i];
        if (line->command[0]) {
            if (fwrite(line->command, 1, 2u, f) != 2u || fputc('\t', f) == EOF) ok = 0;
        }
        if (ok && line->length && fwrite(line->data, 1, line->length, f) != line->length) ok = 0;
        if (ok && i + 1 < lines && fputc('\r', f) == EOF) ok = 0;
    }
    if (!ok) { viewbbc_atomic_abort(f,tmp); return 0; }
    return viewbbc_atomic_commit(f,tmp,path);
}

int viewbbc_file_save_range(const char *path, const ViewBBCDocument *document,
                            size_t start_line, size_t start_column,
                            size_t end_line, size_t end_column) {
    if (!path || !*path || !document || start_line >= document->line_count ||
        end_line >= document->line_count || start_line > end_line) return 0;
    if (start_line == end_line && start_column > end_column) return 0;
    if (start_column > document->lines[start_line].length || end_column > document->lines[end_line].length) return 0;
    char tmp[4096]; FILE *f=viewbbc_atomic_open(path,tmp,sizeof(tmp)); if(!f)return 0;
    int ok=1;
    for(size_t line=start_line;line<=end_line&&ok;++line){const ViewBBCLine*src=&document->lines[line];size_t first=line==start_line?start_column:0u;size_t last=line==end_line?end_column:src->length;if(last<first){ok=0;break;}size_t length=last-first;if(length&&fwrite(src->data+first,1,length,f)!=length)ok=0;if(ok&&line<end_line&&fputc('\r',f)==EOF)ok=0;}
    if (!ok) { viewbbc_atomic_abort(f,tmp); return 0; }
    return viewbbc_atomic_commit(f,tmp,path);
}

