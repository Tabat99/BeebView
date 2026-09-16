#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "atomic_file.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#include <stdint.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
static void set_errno_from_win32(DWORD error) {
    switch (error) {
        case ERROR_ACCESS_DENIED: errno = EACCES; break;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND: errno = ENOENT; break;
        case ERROR_ALREADY_EXISTS:
        case ERROR_FILE_EXISTS: errno = EEXIST; break;
        case ERROR_DISK_FULL: errno = ENOSPC; break;
        default: errno = EIO; break;
    }
}
#endif

FILE *viewbbc_atomic_open(const char *path, char *tmp, size_t tmp_size) {
    if (!path || !*path || !tmp || tmp_size == 0u) { errno = EINVAL; return NULL; }
#ifdef _WIN32
    DWORD pid = GetCurrentProcessId();
    DWORD tick = GetTickCount();
    for (unsigned attempt = 0; attempt < 128u; ++attempt) {
        int written = snprintf(tmp, tmp_size, "%s.tmp.%08lx.%08lx.%02x",
                               path, (unsigned long)pid, (unsigned long)tick, attempt);
        if (written < 0 || (size_t)written >= tmp_size) { errno = ENAMETOOLONG; return NULL; }
        HANDLE handle = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle == INVALID_HANDLE_VALUE) {
            DWORD error = GetLastError();
            if (error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS) continue;
            set_errno_from_win32(error);
            return NULL;
        }
        int fd = _open_osfhandle((intptr_t)handle, _O_BINARY | _O_WRONLY);
        if (fd < 0) { CloseHandle(handle); DeleteFileA(tmp); return NULL; }
        FILE *stream = _fdopen(fd, "wb");
        if (!stream) { int saved = errno; _close(fd); DeleteFileA(tmp); errno = saved; return NULL; }
        return stream;
    }
    errno = EEXIST;
    return NULL;
#else
    int written = snprintf(tmp, tmp_size, "%s.tmp.XXXXXX", path);
    if (written < 0 || (size_t)written >= tmp_size) { errno = ENAMETOOLONG; return NULL; }
    int fd = mkstemp(tmp);
    if (fd < 0) return NULL;
    FILE *stream = fdopen(fd, "wb");
    if (!stream) { int saved = errno; close(fd); unlink(tmp); errno = saved; }
    return stream;
#endif
}

int viewbbc_atomic_commit(FILE *stream, const char *tmp, const char *path) {
    if (!stream || !tmp || !path) { errno = EINVAL; return 0; }
    int saved = 0;
    if (fflush(stream) != 0) saved = errno;
#ifndef _WIN32
    if (!saved && fsync(fileno(stream)) != 0) saved = errno;
#else
    if (!saved && _commit(_fileno(stream)) != 0) saved = errno;
#endif
    if (fclose(stream) != 0 && !saved) saved = errno;
    if (saved) { (void)remove(tmp); errno = saved; return 0; }
#ifdef _WIN32
    if (!MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DWORD error = GetLastError();
        (void)DeleteFileA(tmp);
        set_errno_from_win32(error);
        return 0;
    }
#else
    if (rename(tmp, path) != 0) { saved = errno; (void)remove(tmp); errno = saved; return 0; }
#endif
    return 1;
}

void viewbbc_atomic_abort(FILE *stream, const char *tmp) {
    if (stream) (void)fclose(stream);
    if (tmp && *tmp) (void)remove(tmp);
}
