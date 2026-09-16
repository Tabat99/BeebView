#include "viewbbc/commands.h"

#include <ctype.h>
#include <string.h>

static const char *const command_names[] = {
    "BUFFERSIZE", "CD", "CHANGE", "CLEAR", "CLS", "CONFIG", "COUNT",
    "DIR", "EXIT", "EXPORT", "FOLD", "FORMAT", "HELP", "LIST", "LOAD",
    "LINENUMS", "LS", "MODE", "MOUNT", "NEW", "PRINT", "QUIT", "READ", "REPLACE",
    "ROWCOLS", "SAVE", "SCREEN", "SEARCH", "SETUP", "UNMOUNT", "VER", "WRITE"
};

size_t viewbbc_command_name_count(void) {
    return sizeof(command_names) / sizeof(command_names[0]);
}

const char *viewbbc_command_name_at(size_t index) {
    return index < viewbbc_command_name_count() ? command_names[index] : NULL;
}

static int ascii_ieq(const char *a, size_t a_len, const char *b) {
    size_t b_len = strlen(b);
    if (a_len != b_len) return 0;
    for (size_t i = 0; i < a_len; ++i) {
        if (toupper((unsigned char)a[i]) != toupper((unsigned char)b[i])) return 0;
    }
    return 1;
}

static int parse_argument_to(const char *start, size_t len,
                             char *out, size_t out_size, int *has_argument) {
    if (!start || !out || out_size == 0 || !has_argument) return 0;

    while (len && isspace((unsigned char)*start)) {
        start++;
        len--;
    }
    while (len && isspace((unsigned char)start[len - 1])) len--;

    if (len == 0) {
        out[0] = '\0';
        *has_argument = 0;
        return 1;
    }

    size_t w = 0;
    if (len >= 2 && start[0] == '"' && start[len - 1] == '"') {
        for (size_t r = 1; r + 1 < len; ++r) {
            char ch = start[r];
            if (ch == '"' && r + 2 < len && start[r + 1] == '"') r++;
            if (w + 1u >= out_size) return 0;
            out[w++] = ch;
        }
    } else {
        if (len + 1u > out_size) return 0;
        memcpy(out, start, len);
        w = len;
    }

    out[w] = '\0';
    *has_argument = w != 0;
    return 1;
}

int viewbbc_command_decode_argument(const char *text,
                                    char *out,
                                    size_t out_size,
                                    int *has_argument) {
    if (!text) return 0;
    return parse_argument_to(text, strlen(text), out, out_size, has_argument);
}


static int parse_compact_dir(const char *p, ViewBBCParsedCommand *out) {
    if (!p || !out) return 0;
    size_t length = strlen(p);
    if (length < 4u ||
        toupper((unsigned char)p[0]) != 'D' ||
        toupper((unsigned char)p[1]) != 'I' ||
        toupper((unsigned char)p[2]) != 'R' || p[3] != '/') return 0;

    const char *q = p + 3; /* Keep the slash as part of the DIR option. */
    const char *space = q;
    while (*space && !isspace((unsigned char)*space)) ++space;
    size_t first_len = (size_t)(space - q);
    while (*space && isspace((unsigned char)*space)) ++space;
    size_t rest_len = strlen(space);
    size_t total = first_len + (rest_len ? 1u + rest_len : 0u);
    if (total >= sizeof(out->argument)) return -1;

    if (first_len) memcpy(out->argument, q, first_len);
    size_t used = first_len;
    if (rest_len) {
        out->argument[used++] = ' ';
        memcpy(out->argument + used, space, rest_len);
        used += rest_len;
    }
    out->argument[used] = '\0';
    out->has_argument = used != 0;
    out->type = VIEWBBC_COMMAND_DIR;
    return 1;
}

static int is_compact_cd_prefix(const char *p) {
    if (!p || p[0] == '\0' || p[1] == '\0') return 0;
    if (toupper((unsigned char)p[0]) != 'C' || toupper((unsigned char)p[1]) != 'D') return 0;
    char next = p[2];
    return next == '.' || next == '~' || next == '/' || next == '\\';
}

int viewbbc_command_parse(const char *line, ViewBBCParsedCommand *out) {
    if (!line || !out) return 0;
    *out = (ViewBBCParsedCommand){0};

    const char *p = line;
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) {
        out->type = VIEWBBC_COMMAND_EMPTY;
        return 1;
    }

    int compact_dir = parse_compact_dir(p, out);
    if (compact_dir != 0) {
        if (compact_dir < 0) { out->type = VIEWBBC_COMMAND_INVALID; return 0; }
        return 1;
    }

    if (is_compact_cd_prefix(p)) {
        out->type = VIEWBBC_COMMAND_CD;
        p += 2;
        if (!parse_argument_to(p, strlen(p), out->argument, sizeof(out->argument), &out->has_argument)) {
            out->type = VIEWBBC_COMMAND_INVALID;
            return 0;
        }
        return 1;
    }

    const char *command = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    size_t command_len = (size_t)(p - command);

    if (ascii_ieq(command, command_len, "LOAD") || ascii_ieq(command, command_len, "L")) out->type = VIEWBBC_COMMAND_LOAD;
    else if (ascii_ieq(command, command_len, "SAVE")) out->type = VIEWBBC_COMMAND_SAVE;
    else if (ascii_ieq(command, command_len, "NEW")) out->type = VIEWBBC_COMMAND_NEW;
    else if (ascii_ieq(command, command_len, "READ")) out->type = VIEWBBC_COMMAND_READ;
    else if (ascii_ieq(command, command_len, "WRITE")) out->type = VIEWBBC_COMMAND_WRITE;
    else if (ascii_ieq(command, command_len, "PRINT")) out->type = VIEWBBC_COMMAND_PRINT;
    else if (ascii_ieq(command, command_len, "EXPORT")) out->type = VIEWBBC_COMMAND_EXPORT;
    else if (ascii_ieq(command, command_len, "COUNT")) out->type = VIEWBBC_COMMAND_COUNT;
    else if (ascii_ieq(command, command_len, "SEARCH") || ascii_ieq(command, command_len, "S")) out->type = VIEWBBC_COMMAND_SEARCH;
    else if (ascii_ieq(command, command_len, "CHANGE") || ascii_ieq(command, command_len, "C")) out->type = VIEWBBC_COMMAND_CHANGE;
    else if (ascii_ieq(command, command_len, "REPLACE") || ascii_ieq(command, command_len, "R")) out->type = VIEWBBC_COMMAND_REPLACE;
    else if (ascii_ieq(command, command_len, "FOLD")) out->type = VIEWBBC_COMMAND_FOLD;
    else if (ascii_ieq(command, command_len, "FORMAT")) out->type = VIEWBBC_COMMAND_FORMAT;
    else if (ascii_ieq(command, command_len, "SCREEN")) out->type = VIEWBBC_COMMAND_SCREEN;
    else if (ascii_ieq(command, command_len, "MODE")) out->type = VIEWBBC_COMMAND_MODE;
    else if (ascii_ieq(command, command_len, "SETUP")) out->type = VIEWBBC_COMMAND_SETUP;
    else if (ascii_ieq(command, command_len, "BUFFERSIZE")) out->type = VIEWBBC_COMMAND_BUFFERSIZE;
    else if (ascii_ieq(command, command_len, "LINENUMS")) out->type = VIEWBBC_COMMAND_LINENUMS;
    else if (ascii_ieq(command, command_len, "ROWCOLS")) out->type = VIEWBBC_COMMAND_ROWCOLS;
    else if (ascii_ieq(command, command_len, "VER")) out->type = VIEWBBC_COMMAND_VER;
    else if (ascii_ieq(command, command_len, "HELP") || ascii_ieq(command, command_len, "?")) out->type = VIEWBBC_COMMAND_HELP;
    else if (ascii_ieq(command, command_len, "CLEAR")) out->type = VIEWBBC_COMMAND_CLEAR;
    else if (ascii_ieq(command, command_len, "CLS")) out->type = VIEWBBC_COMMAND_CLS;
    else if (ascii_ieq(command, command_len, "CONFIG")) out->type = VIEWBBC_COMMAND_CONFIG;
    else if (ascii_ieq(command, command_len, "MOUNT")) out->type = VIEWBBC_COMMAND_MOUNT;
    else if (ascii_ieq(command, command_len, "UNMOUNT")) out->type = VIEWBBC_COMMAND_UNMOUNT;
    else if (ascii_ieq(command, command_len, "LIST") || ascii_ieq(command, command_len, "*.") || ascii_ieq(command, command_len, "*CAT")) out->type = VIEWBBC_COMMAND_LIST;
    else if (ascii_ieq(command, command_len, "DIR")) out->type = VIEWBBC_COMMAND_DIR;
    else if (ascii_ieq(command, command_len, "LS")) out->type = VIEWBBC_COMMAND_LS;
    else if (ascii_ieq(command, command_len, "CD")) out->type = VIEWBBC_COMMAND_CD;
    else if (ascii_ieq(command, command_len, "EXIT")) out->type = VIEWBBC_COMMAND_EXIT;
    else if (ascii_ieq(command, command_len, "QUIT")) out->type = VIEWBBC_COMMAND_QUIT;
    else out->type = VIEWBBC_COMMAND_UNKNOWN;

    while (*p && isspace((unsigned char)*p)) p++;
    if (out->type == VIEWBBC_COMMAND_MOUNT || out->type == VIEWBBC_COMMAND_CHANGE ||
        out->type == VIEWBBC_COMMAND_REPLACE) {
        size_t len = strlen(p);
        while (len && isspace((unsigned char)p[len - 1])) len--;
        if (len >= sizeof(out->argument)) {
            out->type = VIEWBBC_COMMAND_INVALID;
            return 0;
        }
        if (len) memcpy(out->argument, p, len);
        out->argument[len] = '\0';
        out->has_argument = len != 0;
        return 1;
    }
    if (!parse_argument_to(p, strlen(p), out->argument, sizeof(out->argument), &out->has_argument)) {
        out->type = VIEWBBC_COMMAND_INVALID;
        return 0;
    }
    return 1;
}
