#ifndef VIEWBBC_ATOMIC_FILE_H
#define VIEWBBC_ATOMIC_FILE_H

#include <stddef.h>
#include <stdio.h>

/* Open a temporary output file in the destination directory. */
FILE *viewbbc_atomic_open(const char *path, char *temporary_path, size_t temporary_path_size);

/* Flush, close and atomically replace path. On failure the old path is kept. */
int viewbbc_atomic_commit(FILE *stream, const char *temporary_path, const char *path);

/* Close (if non-NULL) and remove an uncommitted temporary file. */
void viewbbc_atomic_abort(FILE *stream, const char *temporary_path);

#endif
