#define _XOPEN_SOURCE 700
#include "viewbbc/version.h"
#include "viewbbc/config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#include <fcntl.h>
#else
#include <unistd.h>
#endif

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static int test_mkdir(const char *path) {
#ifdef _WIN32
    return _mkdir(path);
#else
    return mkdir(path, 0700);
#endif
}

static int test_sync_fd(int fd) {
#ifdef _WIN32
    return _commit(fd);
#else
    return fsync(fd);
#endif
}

static long test_process_id(void) {
#ifdef _WIN32
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}

static const char *test_temp_dir(void) {
#ifdef _WIN32
    const char *dir = getenv("TEMP");
    if (dir == NULL || dir[0] == '\0') {
        dir = getenv("TMP");
    }
    return (dir != NULL && dir[0] != '\0') ? dir : ".";
#else
    return "/tmp";
#endif
}

static void test_temp_path(char *out, size_t out_size, const char *stem, const char *extension) {
#ifdef _WIN32
    const char *dir = test_temp_dir();
    const size_t len = strlen(dir);
    const char sep = (len > 0u && (dir[len - 1u] == '\\' || dir[len - 1u] == '/')) ? '\0' : '\\';
    if (sep != '\0') {
        (void)snprintf(out, out_size, "%s%c%s-%ld.%s", dir, sep, stem, test_process_id(), extension);
    } else {
        (void)snprintf(out, out_size, "%s%s-%ld.%s", dir, stem, test_process_id(), extension);
    }
#else
    (void)snprintf(out, out_size, "/tmp/%s-%ld.%s", stem, test_process_id(), extension);
#endif
}

static int test_truncate_fd(int fd, long long size) {
#ifdef _WIN32
    return _chsize_s(fd, size) == 0 ? 0 : -1;
#else
    return ftruncate(fd, (off_t)size);
#endif
}

static int test_make_temp_file(char *out, size_t out_size, const char *stem) {
#ifdef _WIN32
    static unsigned counter = 0u;
    const char *dir = test_temp_dir();
    for (unsigned attempt = 0u; attempt < 1000u; ++attempt) {
        unsigned n = ++counter;
        int written = snprintf(out, out_size, "%s\\%s-%ld-%u.tmp", dir, stem,
                               test_process_id(), n);
        if (written < 0 || (size_t)written >= out_size) return -1;
        int fd = _open(out, _O_CREAT | _O_EXCL | _O_RDWR | _O_BINARY,
                       _S_IREAD | _S_IWRITE);
        if (fd >= 0) return fd;
    }
    return -1;
#else
    int written = snprintf(out, out_size, "/tmp/%s-XXXXXX", stem);
    if (written < 0 || (size_t)written >= out_size) return -1;
    return mkstemp(out);
#endif
}

static int test_make_temp_dir(char *out, size_t out_size, const char *stem) {
#ifdef _WIN32
    static unsigned counter = 0u;
    const char *dir = test_temp_dir();
    for (unsigned attempt = 0u; attempt < 1000u; ++attempt) {
        unsigned n = ++counter;
        int written = snprintf(out, out_size, "%s\\%s-%ld-%u", dir, stem,
                               test_process_id(), n);
        if (written < 0 || (size_t)written >= out_size) return 0;
        if (_mkdir(out) == 0) return 1;
    }
    return 0;
#else
    int written = snprintf(out, out_size, "/tmp/%s-XXXXXX", stem);
    if (written < 0 || (size_t)written >= out_size) return 0;
    return mkdtemp(out) != NULL;
#endif
}

#include "viewbbc/commands.h"
#include "viewbbc/document.h"
#include "viewbbc/dfs.h"
#include "viewbbc/editor.h"
#include "viewbbc/editor_mouse.h"
#include "viewbbc/ruler.h"
#include "viewbbc/editor_commands.h"
#include "viewbbc/file_io.h"
#include "viewbbc/filesystem.h"
#include "viewbbc/output.h"
#include "viewbbc/print.h"
#include "viewbbc/screen.h"
#include "viewbbc/startup.h"
#include "core/editor_command_line.h"

static void screen_row_text(const ViewBBCScreen *screen, int row, char *out, size_t out_size) {
    CHECK(screen != NULL);
    CHECK(out != NULL);
    CHECK(out_size > 0);
    size_t w = 0;
    if (row >= 0 && row < screen->rows) {
        for (int x = 0; x < screen->cols && w + 1u < out_size; ++x) {
            uint32_t ch = viewbbc_screen_get(screen, x, row).ch;
            out[w++] = (char)(ch <= 0x7fu ? ch : '?');
        }
    }
    while (w > 0 && out[w - 1] == ' ') --w;
    out[w] = '\0';
}

static void test_screen(void) {
    ViewBBCScreen screen;
    CHECK(viewbbc_screen_init(&screen, 80, 25));
    viewbbc_screen_put(&screen, 3, 4, 'X', VIEWBBC_ATTR_BOLD);
    ViewBBCCell cell = viewbbc_screen_get(&screen, 3, 4);
    CHECK(cell.ch == 'X');
    CHECK(cell.attr == VIEWBBC_ATTR_BOLD);
    CHECK(viewbbc_screen_resize(&screen, 120, 40));
    CHECK(screen.cols == 120);
    CHECK(screen.rows == 40);
    CHECK(viewbbc_screen_get(&screen, 3, 4).ch == ' ');
    CHECK(viewbbc_screen_resize(&screen, 120, 40));
    CHECK(!viewbbc_screen_resize(&screen, -1, 40));
    CHECK(screen.cols == 120 && screen.rows == 40);
    CHECK(screen.cursor_visible == 1);
    viewbbc_screen_hide_cursor(&screen);
    CHECK(screen.cursor_visible == 0);
    viewbbc_screen_set_cursor(&screen, 7, 8);
    CHECK(screen.cursor_visible == 1);
    CHECK(screen.cursor_x == 7 && screen.cursor_y == 8);
    viewbbc_screen_destroy(&screen);

    /* absurd dimensions must fail instead of overflowing allocation maths */
    CHECK(!viewbbc_screen_init(&screen, -1, 25));
}

static void test_document_editing(void) {
    ViewBBCDocument doc;
    CHECK(viewbbc_document_init(&doc));
    CHECK(viewbbc_document_bytes_used(&doc) == 0);
    CHECK(viewbbc_document_insert_char(&doc, 0, 0, 'a'));
    CHECK(viewbbc_document_insert_char(&doc, 0, 1, 'b'));
    CHECK(viewbbc_document_insert_char(&doc, 0, 2, 'c'));
    CHECK(viewbbc_document_bytes_used(&doc) == 3);
    CHECK(viewbbc_document_overwrite_char(&doc, 0, 1, 'X'));
    CHECK(viewbbc_document_char_at(&doc, 0, 1) == 'X');
    CHECK(viewbbc_document_split_line(&doc, 0, 1));
    CHECK(viewbbc_document_line_count(&doc) == 2);
    CHECK(viewbbc_document_bytes_used(&doc) == 4); /* 3 chars + CR */
    CHECK(viewbbc_document_join_with_previous(&doc, 1));
    CHECK(viewbbc_document_line_count(&doc) == 1);
    CHECK(viewbbc_document_insert_line(&doc, 0));
    CHECK(viewbbc_document_line_count(&doc) == 2);
    CHECK(viewbbc_document_delete_line(&doc, 0));
    CHECK(viewbbc_document_line_count(&doc) == 1);
    viewbbc_document_destroy(&doc);
}

static void test_workspace_limit(void) {
    ViewBBCDocument doc;
    CHECK(viewbbc_document_init_with_limit(&doc, 4));
    CHECK(viewbbc_document_insert_char(&doc, 0, 0, 'A'));
    CHECK(viewbbc_document_insert_char(&doc, 0, 1, 'B'));
    CHECK(viewbbc_document_insert_line(&doc, 1)); /* AB<CR> */
    CHECK(viewbbc_document_insert_char(&doc, 1, 0, 'C'));
    CHECK(viewbbc_document_bytes_used(&doc) == 4);
    CHECK(viewbbc_document_bytes_free(&doc) == 0);
    CHECK(!viewbbc_document_insert_char(&doc, 1, 1, 'D'));
    CHECK(!viewbbc_document_insert_line(&doc, 2));
    viewbbc_document_destroy(&doc);
}

static void test_load_bytes(void) {
    const uint8_t bytes[] = { 'A', '\r', 'B', '\r', '\n', 'C', '\n', 'D' };
    ViewBBCDocument doc;
    CHECK(viewbbc_document_init(&doc));
    CHECK(viewbbc_document_load_bytes(&doc, bytes, sizeof(bytes)));
    CHECK(viewbbc_document_line_count(&doc) == 4);
    CHECK(viewbbc_document_char_at(&doc, 0, 0) == 'A');
    CHECK(viewbbc_document_char_at(&doc, 1, 0) == 'B');
    CHECK(viewbbc_document_char_at(&doc, 2, 0) == 'C');
    CHECK(viewbbc_document_char_at(&doc, 3, 0) == 'D');
    CHECK(viewbbc_document_bytes_used(&doc) == 7); /* 4 chars + 3 logical CRs */
    viewbbc_document_destroy(&doc);
}

static void test_command_parser(void) {
    ViewBBCParsedCommand command;

    CHECK(viewbbc_command_parse("LOAD fred", &command));
    CHECK(command.type == VIEWBBC_COMMAND_LOAD);
    CHECK(strcmp(command.argument, "fred") == 0);

    CHECK(viewbbc_command_parse("LOAD \"My Letter\"", &command));
    CHECK(strcmp(command.argument, "My Letter") == 0);

    CHECK(viewbbc_command_parse("LOAD \"Mike's \"\"final\"\" letter\"", &command));
    CHECK(strcmp(command.argument, "Mike's \"final\" letter") == 0);

    CHECK(viewbbc_command_parse("LOAD \"unmatched", &command));
    CHECK(strcmp(command.argument, "\"unmatched") == 0);

    CHECK(viewbbc_command_parse("LOAD odd\"name", &command));
    CHECK(strcmp(command.argument, "odd\"name") == 0);

    CHECK(viewbbc_command_parse("SAVE", &command));
    CHECK(command.type == VIEWBBC_COMMAND_SAVE);
    CHECK(!command.has_argument);

    CHECK(viewbbc_command_parse("READ notes.txt", &command));
    CHECK(command.type == VIEWBBC_COMMAND_READ);
    CHECK(strcmp(command.argument, "notes.txt") == 0);
    CHECK(viewbbc_command_parse("WRITE out.txt 1 2", &command));
    CHECK(command.type == VIEWBBC_COMMAND_WRITE);
    CHECK(viewbbc_command_parse("PRINT", &command));
    CHECK(command.type == VIEWBBC_COMMAND_PRINT && !command.has_argument);
    CHECK(viewbbc_command_parse("PRINT printer:office", &command));
    CHECK(command.type == VIEWBBC_COMMAND_PRINT && command.has_argument);
    CHECK(strcmp(command.argument, "printer:office") == 0);
    CHECK(viewbbc_command_parse("EXPORT notes.txt", &command));
    CHECK(command.type == VIEWBBC_COMMAND_EXPORT && command.has_argument);
    CHECK(strcmp(command.argument, "notes.txt") == 0);
    CHECK(viewbbc_command_parse("COUNT", &command));
    CHECK(command.type == VIEWBBC_COMMAND_COUNT);
    CHECK(viewbbc_command_parse("SEARCH \"old phrase\"", &command));
    CHECK(command.type == VIEWBBC_COMMAND_SEARCH);
    CHECK(strcmp(command.argument, "old phrase") == 0);
    CHECK(viewbbc_command_parse("CHANGE old new", &command));
    CHECK(command.type == VIEWBBC_COMMAND_CHANGE);
    CHECK(viewbbc_command_parse("REPLACE old new", &command));
    CHECK(command.type == VIEWBBC_COMMAND_REPLACE);
    CHECK(viewbbc_command_parse("R old new", &command));
    CHECK(command.type == VIEWBBC_COMMAND_REPLACE);
    CHECK(viewbbc_command_parse("FOLD 0", &command));
    CHECK(command.type == VIEWBBC_COMMAND_FOLD);
    CHECK(viewbbc_command_parse("CHANGE \"old phrase\" \"new phrase\"", &command));
    CHECK(strcmp(command.argument, "\"old phrase\" \"new phrase\"") == 0);
    CHECK(viewbbc_command_parse("SCREEN", &command));
    CHECK(command.type == VIEWBBC_COMMAND_SCREEN);
    CHECK(viewbbc_command_parse("MODE 3", &command));
    CHECK(command.type == VIEWBBC_COMMAND_MODE);
    CHECK(viewbbc_command_parse("SETUP FJW", &command));
    CHECK(command.type == VIEWBBC_COMMAND_SETUP && command.has_argument);
    CHECK(strcmp(command.argument, "FJW") == 0);
    CHECK(viewbbc_command_parse("BUFFERSIZE 10MB", &command));
    CHECK(command.type == VIEWBBC_COMMAND_BUFFERSIZE && command.has_argument);
    CHECK(strcmp(command.argument, "10MB") == 0);
    CHECK(viewbbc_command_parse("LINENUMS ON", &command));
    CHECK(command.type == VIEWBBC_COMMAND_LINENUMS && command.has_argument);
    CHECK(strcmp(command.argument, "ON") == 0);
    CHECK(viewbbc_command_parse("ROWCOLS ON", &command));
    CHECK(command.type == VIEWBBC_COMMAND_ROWCOLS && command.has_argument);
    CHECK(strcmp(command.argument, "ON") == 0);
    CHECK(viewbbc_command_parse("VER", &command));
    CHECK(command.type == VIEWBBC_COMMAND_VER && !command.has_argument);
    CHECK(viewbbc_command_parse("CONFIG", &command));
    CHECK(command.type == VIEWBBC_COMMAND_CONFIG && !command.has_argument);
    CHECK(viewbbc_command_parse("CONFIG RESET", &command));
    CHECK(command.type == VIEWBBC_COMMAND_CONFIG && command.has_argument);
    CHECK(viewbbc_command_parse("CONFIG LOC", &command));
    CHECK(command.type == VIEWBBC_COMMAND_CONFIG && command.has_argument);
    CHECK(strcmp(command.argument, "LOC") == 0);

    CHECK(viewbbc_command_parse("HELP", &command));
    CHECK(command.type == VIEWBBC_COMMAND_HELP);
    CHECK(!command.has_argument);

    CHECK(viewbbc_command_parse("?", &command));
    CHECK(command.type == VIEWBBC_COMMAND_HELP);
    CHECK(viewbbc_command_parse("CLEAR", &command));
    CHECK(command.type == VIEWBBC_COMMAND_CLEAR);
    CHECK(!command.has_argument);
    CHECK(viewbbc_command_parse("CLS", &command));
    CHECK(command.type == VIEWBBC_COMMAND_CLS);
    CHECK(!command.has_argument);

    CHECK(viewbbc_command_parse("MOUNT SSD \"disc image.ssd\"", &command));
    CHECK(command.type == VIEWBBC_COMMAND_MOUNT);
    CHECK(strcmp(command.argument, "SSD \"disc image.ssd\"") == 0);

    CHECK(viewbbc_command_parse("MOUNT \"SSD my disc.ssd\"", &command));
    CHECK(command.type == VIEWBBC_COMMAND_MOUNT);
    CHECK(strcmp(command.argument, "\"SSD my disc.ssd\"") == 0);

    CHECK(viewbbc_command_parse("UNMOUNT", &command));
    CHECK(command.type == VIEWBBC_COMMAND_UNMOUNT);
    CHECK(viewbbc_command_parse("*CAT", &command));
    CHECK(command.type == VIEWBBC_COMMAND_LIST);
    CHECK(viewbbc_command_parse("*.", &command));
    CHECK(command.type == VIEWBBC_COMMAND_LIST);
    CHECK(viewbbc_command_parse("DIR /W", &command));
    CHECK(command.type == VIEWBBC_COMMAND_DIR);
    CHECK(strcmp(command.argument, "/W") == 0);
    CHECK(viewbbc_command_parse("dir/w", &command));
    CHECK(command.type == VIEWBBC_COMMAND_DIR);
    CHECK(strcmp(command.argument, "/w") == 0);
    CHECK(viewbbc_command_parse("DIR/W /A", &command));
    CHECK(command.type == VIEWBBC_COMMAND_DIR);
    CHECK(strcmp(command.argument, "/W /A") == 0);
    CHECK(viewbbc_command_parse("EXIT", &command));
    CHECK(command.type == VIEWBBC_COMMAND_EXIT);
    CHECK(viewbbc_command_parse("quit", &command));
    CHECK(command.type == VIEWBBC_COMMAND_QUIT);
    CHECK(viewbbc_command_parse("LS -la", &command));
    CHECK(command.type == VIEWBBC_COMMAND_LS);
    CHECK(strcmp(command.argument, "-la") == 0);
    CHECK(viewbbc_command_parse("cd..", &command));
    CHECK(command.type == VIEWBBC_COMMAND_CD);
    CHECK(strcmp(command.argument, "..") == 0);
    CHECK(viewbbc_command_parse("cd~/Documents", &command));
    CHECK(command.type == VIEWBBC_COMMAND_CD);
    CHECK(strcmp(command.argument, "~/Documents") == 0);
}


static void test_document_command_operations(void) {
    ViewBBCDocument doc;
    CHECK(viewbbc_document_init(&doc));
    const uint8_t initial[] = "one two\rthree two";
    CHECK(viewbbc_document_load_bytes(&doc, initial, sizeof(initial) - 1u));
    CHECK(viewbbc_document_count_words(&doc) == 4);

    size_t line = 0, column = 0;
    const uint8_t needle[] = "two";
    CHECK(viewbbc_document_find(&doc, needle, 3, 0, 0, &line, &column));
    CHECK(line == 0 && column == 4);

    size_t changed = 0;
    const uint8_t replacement[] = "TWO";
    CHECK(viewbbc_document_replace_all(&doc, needle, 3, replacement, 3, &changed));
    CHECK(changed == 2);
    CHECK(viewbbc_document_char_at(&doc, 0, 4) == 'T');
    CHECK(viewbbc_document_char_at(&doc, 1, 6) == 'T');

    const uint8_t inserted[] = "X\rY";
    CHECK(viewbbc_document_insert_bytes_at(&doc, 0, 3, inserted, sizeof(inserted) - 1u));
    CHECK(viewbbc_document_line_count(&doc) == 3);
    CHECK(viewbbc_document_char_at(&doc, 0, 3) == 'X');
    CHECK(viewbbc_document_char_at(&doc, 1, 0) == 'Y');
    viewbbc_document_destroy(&doc);
}


static void test_dfs_read_only(void) {
    const char *path = "viewbbc_test_image.ssd";
    uint8_t image[80u * 10u * 256u] = {0};
    memcpy(image, "TESTDISC", 8);
    memcpy(image + 8, "HELLO  ", 7);
    image[15] = '$';

    uint8_t *info = image + 256;
    info[5] = 8;       /* one file */
    info[6] = 3;       /* 800 sectors: high two bits in low bits of byte 6 */
    info[7] = 32;      /* 800 & 0xff */
    info[8 + 4] = 3;   /* length = 3 */
    info[8 + 7] = 2;   /* start sector = 2 */
    memcpy(image + 512, "ABC", 3);

    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(image, 1, sizeof(image), f) == sizeof(image));
    CHECK(fclose(f) == 0);

    ViewBBCDFSImage dfs;
    CHECK(viewbbc_dfs_open(&dfs, path));
    CHECK(dfs.side_count == 1);
    CHECK(dfs.sides[0].file_count == 1);
    const ViewBBCDFSFile *file = viewbbc_dfs_find_file(&dfs, "HELLO");
    CHECK(file != NULL);
    CHECK(strcmp(file->name, "$.HELLO") == 0);
    CHECK(file->length == 3);

    /* Exercise all packed upper-bit fields.  A previous decoder accidentally
     * swapped the length and exec bit-pairs, which rejected real SSD images. */
    viewbbc_dfs_close(&dfs);
    CHECK(remove(path) == 0);

    uint8_t compact[306u * 256u] = {0};
    memcpy(compact, "WELCOME ", 8);
    memcpy(compact + 8, "SKETCH ", 7);
    compact[15] = 'W';
    info = compact + 256;
    info[5] = 8;
    info[6] = 3;
    info[7] = 32; /* catalogue says 800 sectors; shortened SSD stores used tail only */
    info[8 + 0] = 0x00;
    info[8 + 1] = 0x19;
    info[8 + 2] = 0x1f;
    info[8 + 3] = 0x80;
    info[8 + 4] = 0x50;
    info[8 + 5] = 0x07;
    info[8 + 6] = 0xcd; /* start=3, load=3, length=0, exec=3 */
    info[8 + 7] = 0x1e; /* start sector 0x31e would be invalid here if decoded wrong */

    /* For this compact regression, use a packed byte matching a real-world
     * entry whose start sector is 286: low pair=1 and low byte=0x1e. */
    info[8 + 6] = 0xc1; /* start=1, load=0, length=0, exec=3 */
    memcpy(compact + 286u * 256u, "XYZ", 3);
    info[8 + 4] = 3;
    info[8 + 5] = 0;

    f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(compact, 1, sizeof(compact), f) == sizeof(compact));
    CHECK(fclose(f) == 0);
    CHECK(viewbbc_dfs_open(&dfs, path));
    file = viewbbc_dfs_find_file(&dfs, "W.SKETCH");
    CHECK(file != NULL);
    CHECK(file->start_sector == 286);
    CHECK(file->length == 3);
    CHECK(file->exec_address == 0x3801fu);

    uint8_t *data = NULL;
    size_t length = 0;
    CHECK(viewbbc_dfs_extract_file(&dfs, file, &data, &length));
    CHECK(length == 3);
    CHECK(memcmp(data, "XYZ", 3) == 0);
    free(data);
    viewbbc_dfs_close(&dfs);
    CHECK(remove(path) == 0);
}

static void editor_command(ViewBBCEditor *editor, const char *text) {
    size_t n = strlen(text);
    CHECK(n <= VIEWBBC_COMMAND_MAX);
    memcpy(editor->command_buffer, text, n + 1u);
    editor->command_length = n;
    editor->command_cursor = n;
    viewbbc_editor_execute_command(editor);
}

static void test_native_path_separators(void) {
    ViewBBCFilesystemState fs;
    CHECK(viewbbc_fs_init(&fs));

    char *path = NULL;
    CHECK(viewbbc_fs_resolve_native(&fs, "one\\two/three", &path));
    CHECK(strstr(path, "/one/two/three") != NULL);
    free(path);

    path = NULL;
    CHECK(viewbbc_fs_resolve_native(&fs, "\\tmp\\viewbbc/path", &path));
    CHECK(strcmp(path, "/tmp/viewbbc/path") == 0);
    free(path);

    const char *home = getenv("HOME");
    if (home && *home) {
        path = NULL;
        CHECK(viewbbc_fs_resolve_native(&fs, "~\\Documents\\VIEW", &path));
        size_t home_len = strlen(home);
        CHECK(strncmp(path, home, home_len) == 0);
        CHECK(strcmp(path + home_len, "/Documents/VIEW") == 0);
        free(path);
    }

    viewbbc_fs_destroy(&fs);
}

static void test_native_directory_listing(void) {
    const char *root = "viewbbc_test_listing";
    const char *dir_a = "viewbbc_test_listing/alpha_dir";
    const char *dir_z = "viewbbc_test_listing/zeta_dir";
    const char *file_a = "viewbbc_test_listing/alpha.txt";
    const char *file_z = "viewbbc_test_listing/zeta.txt";

    (void)remove(file_a);
    (void)remove(file_z);
    (void)rmdir(dir_a);
    (void)rmdir(dir_z);
    (void)rmdir(root);
    CHECK(test_mkdir(root) == 0);
    CHECK(test_mkdir(dir_z) == 0);
    CHECK(test_mkdir(dir_a) == 0);

    FILE *f = fopen(file_z, "wb");
    CHECK(f != NULL);
    CHECK(fputs("z", f) >= 0);
    CHECK(fclose(f) == 0);
    f = fopen(file_a, "wb");
    CHECK(f != NULL);
    CHECK(fputs("a", f) >= 0);
    CHECK(fclose(f) == 0);

    ViewBBCFilesystemState fs;
    CHECK(viewbbc_fs_init(&fs));
    char error[128];
    CHECK(viewbbc_fs_chdir_native(&fs, root, error, sizeof(error)));

    ViewBBCCommandOutput output;
    CHECK(viewbbc_fs_list_native(&fs, VIEWBBC_LIST_STYLE_DIR, "", &output, error, sizeof(error)));
    CHECK(output.line_count == 5);
    CHECK(strcmp(output.lines[1], " [alpha_dir]") == 0);
    CHECK(strcmp(output.lines[2], " [zeta_dir]") == 0);
    CHECK(strcmp(output.lines[3], " alpha.txt") == 0);
    CHECK(strcmp(output.lines[4], " zeta.txt") == 0);

    CHECK(viewbbc_fs_list_native(&fs, VIEWBBC_LIST_STYLE_DIR, "/W", &output, error, sizeof(error)));
    CHECK(output.line_count == 2);
    CHECK(output.lines[1][0] == ' ');
    CHECK(strstr(output.lines[1], "[alpha_dir]") != NULL);
    CHECK(strstr(output.lines[1], "[zeta_dir]") != NULL);
    CHECK(strstr(output.lines[1], "alpha.txt") != NULL);
    CHECK(strstr(output.lines[1], "zeta.txt") != NULL);

    /* /B retains DOS-style bare output, but still sorts folders before files. */
    CHECK(viewbbc_fs_list_native(&fs, VIEWBBC_LIST_STYLE_DIR, "/B", &output, error, sizeof(error)));
    CHECK(output.line_count == 4);
    CHECK(strcmp(output.lines[0], "alpha_dir") == 0);
    CHECK(strcmp(output.lines[1], "zeta_dir") == 0);
    CHECK(strcmp(output.lines[2], "alpha.txt") == 0);
    CHECK(strcmp(output.lines[3], "zeta.txt") == 0);

    CHECK(viewbbc_fs_list_native(&fs, VIEWBBC_LIST_STYLE_LS, "-l", &output, error, sizeof(error)));
    CHECK(output.line_count == 5);
    CHECK(output.lines[1][0] == ' ' && output.lines[1][1] == 'd');
    CHECK(output.lines[2][0] == ' ' && output.lines[2][1] == 'd');
    CHECK(output.lines[3][0] == ' ' && output.lines[3][1] == '-');

    viewbbc_fs_destroy(&fs);
    CHECK(remove(file_a) == 0);
    CHECK(remove(file_z) == 0);
    CHECK(rmdir(dir_a) == 0);
    CHECK(rmdir(dir_z) == 0);
    CHECK(rmdir(root) == 0);
}

static void test_filesystem_commands(void) {
    const char *path = "viewbbc_test_mount.ssd";
    uint8_t image[80u * 10u * 256u] = {0};
    memcpy(image, "TESTDISC", 8);
    memcpy(image + 8, "HELLO  ", 7);
    image[15] = '$';
    uint8_t *info = image + 256;
    info[5] = 8;
    info[6] = 3;
    info[7] = 32;
    info[8 + 4] = 3;
    info[8 + 7] = 2;
    memcpy(image + 512, "ABC", 3);

    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite(image, 1, sizeof(image), f) == sizeof(image));
    CHECK(fclose(f) == 0);

    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));

    editor_command(&editor, "CD");
    CHECK(editor.command_output.line_count == 1);
    CHECK(editor.filesystem.native_cwd != NULL);

    editor_command(&editor, "MOUNT definitely-not-present.ssd");
    CHECK(editor.image_mounted == 0);
    CHECK(strcmp(editor.status_message, "Disc image not found") == 0);
    CHECK(editor.command_output.line_count == 1);
    CHECK(strstr(editor.command_output.lines[0], "definitely-not-present.ssd") != NULL);

    editor_command(&editor, "MOUNT viewbbc_test_mount.ssd");
    CHECK(editor.image_mounted == 1);
    CHECK(editor.mounted_image.type == VIEWBBC_DFS_TYPE_SSD);
    CHECK(editor.filesystem.dfs_directory == '$');
    viewbbc_editor_render(&editor);
    char header[VIEWBBC_CONSOLE_LINE_MAX + 1];
    screen_row_text(&editor.screen, 3, header, sizeof(header));
    CHECK(strstr(header, "File System: DFS SSD") == header);
    CHECK(editor.mounted_image_path != NULL);
    CHECK(strstr(editor.mounted_image_path, "viewbbc_test_mount.ssd") != NULL);

    editor_command(&editor, "MOUNT");
    CHECK(editor.command_output.line_count >= 2);

    editor_command(&editor, "LOAD HELLO");
    CHECK(viewbbc_document_line_length(&editor.document, 0) == 3);
    CHECK(viewbbc_document_char_at(&editor.document, 0, 0) == 'A');
    CHECK(editor.current_source_read_only == 1);

    editor_command(&editor, "SAVE should-not-be-created");
    CHECK(strstr(editor.status_message, "read only") != NULL);

    editor_command(&editor, "CD A");
    CHECK(editor.filesystem.dfs_directory == 'A');
    editor_command(&editor, "CD..");
    CHECK(editor.filesystem.dfs_directory == '$');

    editor_command(&editor, "LIST");
    CHECK(editor.command_output.line_count >= 1);

    editor_command(&editor, "DIR /W");
    CHECK(editor.command_output.line_count >= 2);
    CHECK(editor.command_output.lines[1][0] == ' ');
    CHECK(strstr(editor.command_output.lines[1], "HELLO") != NULL);

    editor_command(&editor, "DIR/W");
    CHECK(editor.command_output.line_count >= 2);
    CHECK(strstr(editor.command_output.lines[1], "HELLO") != NULL);

    editor_command(&editor, "UNMOUNT");
    CHECK(editor.image_mounted == 0);

    viewbbc_editor_destroy(&editor);
    CHECK(remove(path) == 0);
    (void)remove("should-not-be-created");
}


static void type_command(ViewBBCEditor *editor, const char *text) {
    CHECK(editor != NULL);
    CHECK(text != NULL);
    for (size_t i = 0; text[i] != '\0'; ++i) {
        viewbbc_editor_insert_char(editor, (uint8_t)text[i]);
    }
    viewbbc_editor_handle_key(editor, VIEWBBC_KEY_RETURN);
}

static void test_command_console_history(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 10));

    char row[128];
    screen_row_text(&editor.screen, 3, row, sizeof(row));
    CHECK(strcmp(row, "File System: Native") == 0);
    screen_row_text(&editor.screen, 4, row, sizeof(row));
    CHECK(strcmp(row, "Editing: No File") == 0);

    /* Exercise the real keyboard/RETURN path, not just the parser directly. */
    type_command(&editor, "cd");
    CHECK(strcmp(editor.status_message, "Current folder") == 0);
    CHECK(editor.console.count >= 2);
    CHECK(strcmp(viewbbc_console_line(&editor.console, 0), "=>cd") == 0);

    type_command(&editor, "mount");
    CHECK(strcmp(editor.status_message, "No disc mounted") == 0);
    CHECK(strcmp(editor.status_message, "Mistake") != 0);

    type_command(&editor, "cls");
    CHECK(editor.console.count == 0);
    CHECK(editor.status_message[0] == '\0');
    CHECK(viewbbc_screen_get(&editor.screen, 0, 0).ch == 'B');

    /* Once command history exceeds the display, the command screen follows
       the newest line and leaves the live prompt on the bottom row. */
    for (int i = 0; i < 12; ++i) type_command(&editor, "");
    CHECK(editor.screen.cursor_y == editor.screen.rows - 1);
    CHECK(viewbbc_screen_get(&editor.screen, 0, editor.screen.rows - 1).ch == '=');
    CHECK(viewbbc_screen_get(&editor.screen, 1, editor.screen.rows - 1).ch == '>');
    CHECK(viewbbc_screen_get(&editor.screen, 0, 0).ch != 'B');

    viewbbc_editor_destroy(&editor);
}

static void test_view_key_semantics(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    CHECK(editor.mode == VIEWBBC_MODE_COMMAND);
    CHECK(viewbbc_document_workspace_limit(&editor.document) == VIEWBBC_DEFAULT_WORKSPACE_LIMIT);

    viewbbc_editor_insert_char(&editor, 'L');
    viewbbc_editor_insert_char(&editor, 'O');
    CHECK(editor.command_length == 2);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_ESCAPE);
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);

    CHECK(editor.format_mode == 1);
    CHECK(editor.justify_mode == 1);
    CHECK(editor.insert_mode == 1);

    /* TAB follows the ruler's '*' stops: columns 7, 15, 23, ... . */
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(editor.cursor_column == 7);
    CHECK(viewbbc_document_line_length(&editor.document, 0) == 7);
    for (size_t i = 0; i < 7; ++i) {
        CHECK(viewbbc_document_char_at(&editor.document, 0, i) == ' ');
    }
    viewbbc_editor_insert_char(&editor, 'T');
    CHECK(viewbbc_document_char_at(&editor.document, 0, 7) == 'T');
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(editor.cursor_column == 15);
    CHECK(viewbbc_document_line_length(&editor.document, 0) == 15);

    /* Keep the established editing-semantics checks independent of TAB. */
    viewbbc_document_clear(&editor.document);
    editor.cursor_line = 0;
    editor.cursor_column = 0;
    editor.viewport_top = 0;
    editor.viewport_left = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F4);
    CHECK(editor.insert_mode == 0);
    viewbbc_editor_insert_char(&editor, 'a');
    viewbbc_editor_insert_char(&editor, 'b');
    viewbbc_editor_insert_char(&editor, 'c');
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_F4);
    viewbbc_editor_insert_char(&editor, 'X');
    CHECK(viewbbc_document_line_length(&editor.document, 0) == 3);
    CHECK(viewbbc_document_char_at(&editor.document, 0, 0) == 'X');

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F4);
    CHECK(editor.insert_mode == 1);
    viewbbc_editor_insert_char(&editor, 'Y');
    CHECK(viewbbc_document_line_length(&editor.document, 0) == 4);

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_INSERT);
    CHECK(editor.insert_mode == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_INSERT);
    CHECK(editor.insert_mode == 1);

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_BACKSPACE);
    CHECK(viewbbc_document_line_length(&editor.document, 0) == 4);
    CHECK(viewbbc_document_char_at(&editor.document, 0, 1) == ' ');

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_RETURN);
    CHECK(editor.cursor_line == 1);
    CHECK(editor.cursor_column == 0);
    CHECK(viewbbc_document_line_count(&editor.document) == 2);

    viewbbc_editor_insert_char(&editor, '1');
    viewbbc_editor_insert_char(&editor, '2');
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_F4);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_RIGHT);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F6);
    CHECK(viewbbc_document_line_count(&editor.document) == 3);

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_ESCAPE);
    CHECK(editor.mode == VIEWBBC_MODE_COMMAND);
    CHECK(viewbbc_document_line_count(&editor.document) == 3);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_ESCAPE);
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);

    /* HELP and ? are aliases in command mode and produce a persistent list. */
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_ESCAPE);
    CHECK(editor.mode == VIEWBBC_MODE_COMMAND);
    editor.command_length = 1;
    editor.command_buffer[0] = '?';
    editor.command_buffer[1] = '\0';
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_RETURN);
    CHECK(editor.help_visible == 1);
    CHECK(editor.status_message[0] == '\0');
    int found_version = 0;
    int found_help_subject = 0;
    int found_conf_subject = 0;
    for (size_t i = 0; i < editor.console.count; ++i) {
        const char *line = viewbbc_console_line(&editor.console, i);
        if (!line) continue;
        if (strcmp(line, VIEWBBC_PRODUCT_NAME " " VIEWBBC_VERSION_STRING) == 0) found_version = 1;
        if (strstr(line, "HELP <subject>") != NULL) found_help_subject = 1;
        if (strstr(line, "Conf") != NULL) found_conf_subject = 1;
    }
    CHECK(found_version);
    CHECK(found_help_subject);
    CHECK(found_conf_subject);

    ViewBBCHelpTopic help_topic = VIEWBBC_HELP_TOPIC_ALL;
    CHECK(viewbbc_help_topic_parse("edit", &help_topic));
    CHECK(help_topic == VIEWBBC_HELP_TOPIC_EDIT);
    CHECK(viewbbc_help_topic_parse("FILES", &help_topic));
    CHECK(help_topic == VIEWBBC_HELP_TOPIC_FILES);
    CHECK(viewbbc_help_topic_parse("Keys", &help_topic));
    CHECK(help_topic == VIEWBBC_HELP_TOPIC_KEYS);
    CHECK(viewbbc_help_topic_parse("Conf", &help_topic));
    CHECK(help_topic == VIEWBBC_HELP_TOPIC_CONF);
    CHECK(!viewbbc_help_topic_parse("nonsense", &help_topic));

    CHECK(editor.output_paging == 0);

    /* BREAK is non-destructive and always returns to a clean COMMAND screen. */
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_ESCAPE);
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_BREAK);
    CHECK(editor.mode == VIEWBBC_MODE_COMMAND);
    CHECK(editor.console.count == 0);
    CHECK(viewbbc_document_line_count(&editor.document) == 3);

    /* Home/End and page navigation are TEXT-mode cursor operations. */
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_ESCAPE);
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);
    viewbbc_document_clear(&editor.document);
    for (size_t i = 1; i < 60; ++i) {
        CHECK(viewbbc_document_insert_line(&editor.document, i));
    }
    editor.cursor_line = 0;
    editor.cursor_column = 0;
    editor.viewport_top = 0;
    editor.viewport_left = 0;
    CHECK(viewbbc_document_overwrite_char(&editor.document, 0, 0, 'A'));
    CHECK(viewbbc_document_overwrite_char(&editor.document, 0, 1, 'B'));
    CHECK(viewbbc_document_overwrite_char(&editor.document, 0, 2, 'C'));
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_END);
    CHECK(editor.cursor_column == 3);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_HOME);
    CHECK(editor.cursor_column == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_PAGE_DOWN);
    CHECK(editor.cursor_line == 24);
    CHECK(editor.viewport_top == 24);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_PAGE_UP);
    CHECK(editor.cursor_line == 0);
    CHECK(editor.viewport_top == 0);

    /* A modified document must be confirmed before an external quit request. */
    CHECK(editor.file_modified == 1);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_QUIT);
    CHECK(editor.running == 1);
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_EXIT);
    viewbbc_editor_insert_char(&editor, 'n');
    CHECK(editor.running == 1);
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_NONE);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_QUIT);
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_EXIT);
    viewbbc_editor_insert_char(&editor, 'Y');
    CHECK(editor.running == 0);
    viewbbc_editor_destroy(&editor);
}

static void test_modified_load_and_exit_commands(void) {
    const char *first = "viewbbc_dirty_first.txt";
    const char *second = "viewbbc_dirty_second.txt";
    FILE *f = fopen(first, "wb");
    CHECK(f != NULL);
    CHECK(fwrite("FIRST", 1, 5, f) == 5);
    CHECK(fclose(f) == 0);
    f = fopen(second, "wb");
    CHECK(f != NULL);
    CHECK(fwrite("SECOND", 1, 6, f) == 6);
    CHECK(fclose(f) == 0);

    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor_command(&editor, "LOAD viewbbc_dirty_first.txt");
    CHECK(editor.file_modified == 0);
    editor.mode = VIEWBBC_MODE_TEXT;
    editor.cursor_column = viewbbc_document_line_length(&editor.document, 0);
    viewbbc_editor_insert_char(&editor, '!');
    CHECK(editor.file_modified == 1);

    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "LOAD viewbbc_dirty_second.txt");
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_LOAD);
    CHECK(viewbbc_document_char_at(&editor.document, 0, 0) == 'F');
    viewbbc_editor_insert_char(&editor, 'N');
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_NONE);
    CHECK(viewbbc_document_char_at(&editor.document, 0, 0) == 'F');

    editor_command(&editor, "LOAD viewbbc_dirty_second.txt");
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_LOAD);
    viewbbc_editor_insert_char(&editor, 'y');
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_NONE);
    CHECK(editor.file_modified == 0);
    CHECK(viewbbc_document_char_at(&editor.document, 0, 0) == 'S');

    editor.mode = VIEWBBC_MODE_TEXT;
    viewbbc_editor_insert_char(&editor, '!');
    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "QUIT");
    CHECK(editor.running == 1);
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_EXIT);
    viewbbc_editor_insert_char(&editor, 'n');
    CHECK(editor.running == 1);
    editor_command(&editor, "EXIT");
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_EXIT);
    viewbbc_editor_insert_char(&editor, 'y');
    CHECK(editor.running == 0);

    viewbbc_editor_destroy(&editor);
    CHECK(remove(first) == 0);
    CHECK(remove(second) == 0);
}


static void test_command_paging(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 10));
    viewbbc_document_clear(&editor.document);
    for (size_t i = 0; i < 35; ++i) {
        if (i > 0) CHECK(viewbbc_document_insert_line(&editor.document, i));
        CHECK(viewbbc_document_insert_char(&editor.document, i, 0, (uint8_t)('A' + (i % 26))));
    }

    editor_command(&editor, "SCREEN");
    CHECK(editor.output_paging == 1);
    viewbbc_editor_render(&editor);
    CHECK(editor.screen.cursor_visible == 0);
    size_t first_next = editor.output_page_next;
    CHECK(first_next > 0 && first_next < editor.command_output.line_count);

    /* SPACE advances exactly one page and pauses again when more remains. */
    viewbbc_editor_insert_char(&editor, ' ');
    CHECK(editor.output_page_next > first_next);
    CHECK(editor.output_paging == 1);

    /* ESCAPE drains every remaining page without pausing again. */
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_ESCAPE);
    CHECK(editor.output_paging == 0);
    CHECK(editor.output_page_next == editor.command_output.line_count);
    viewbbc_editor_render(&editor);
    CHECK(editor.screen.cursor_visible == 1);
    CHECK(editor.mode == VIEWBBC_MODE_COMMAND);

    viewbbc_editor_destroy(&editor);
}


static void test_view_command_batch(void) {
    const char *path = "viewbbc_test_read.txt";
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    CHECK(fwrite("alpha beta\r", 1, 11, f) == 11);
    CHECK(fclose(f) == 0);

    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));

    editor_command(&editor, "READ viewbbc_test_read.txt");
    CHECK(editor.file_modified == 1);
    CHECK(viewbbc_document_line_count(&editor.document) == 2);

    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 6u));
    editor_command(&editor, "READ viewbbc_test_read.txt 1");
    CHECK(strstr(editor.status_message, "marker") != NULL);
    CHECK(viewbbc_document_line_count(&editor.document) >= 3);


    CHECK(viewbbc_document_load_bytes(&editor.document, (const uint8_t *)"alpha beta\r", 11));
    editor.file_modified = 0;

    editor_command(&editor, "COUNT");
    CHECK(editor.command_output.line_count == 1);
    CHECK(strcmp(editor.command_output.lines[0], "Words: 2") == 0);

    editor_command(&editor, "CHANGE beta gamma");
    CHECK(editor.command_output.line_count == 1);
    CHECK(strcmp(editor.command_output.lines[0], "Changed: 1") == 0);

    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "SEARCH gamma");
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);
    CHECK(editor.cursor_line == 0);
    CHECK(editor.cursor_column == 6);

    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "MODE");
    CHECK(strcmp(editor.command_output.lines[0], "Screen mode 3") == 0);
    editor_command(&editor, "MODE 7");
    CHECK(strstr(editor.status_message, "MODE 3") != NULL);

    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "SCREEN");
    CHECK(editor.command_output.line_count >= 2);
    CHECK(strstr(editor.command_output.lines[0], "alpha gamma") != NULL);

    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 6u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 0u, 11u));
    editor_command(&editor, "WRITE viewbbc_test_write.txt 1 2");
    FILE *wf = fopen("viewbbc_test_write.txt", "rb");
    CHECK(wf != NULL);
    char written[16] = {0};
    CHECK(fread(written, 1, sizeof(written) - 1u, wf) == 5u);
    CHECK(fclose(wf) == 0);
    CHECK(strcmp(written, "gamma") == 0);

    editor_command(&editor, "CLEAR");
    CHECK(!editor.markers.marker[0].set);
    CHECK(!editor.markers.marker[1].set);

    viewbbc_editor_destroy(&editor);
    CHECK(remove(path) == 0);
    CHECK(remove("viewbbc_test_write.txt") == 0);
}


static void test_markers(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;

    viewbbc_editor_insert_char(&editor, 'A');
    viewbbc_editor_insert_char(&editor, 'B');
    CHECK(editor.cursor_column == 2);

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F7);
    CHECK(editor.marker_input == VIEWBBC_MARKER_INPUT_SET);
    viewbbc_editor_insert_char(&editor, '1');
    CHECK(editor.marker_input == VIEWBBC_MARKER_INPUT_NONE);

    editor.cursor_column = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F6);
    viewbbc_editor_insert_char(&editor, '1');
    CHECK(editor.cursor_line == 0);
    CHECK(editor.cursor_column == 2);

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F6);
    viewbbc_editor_insert_char(&editor, '6');
    CHECK(strstr(editor.status_message, "not set") != NULL);

    viewbbc_editor_destroy(&editor);
}


static void test_format_block(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;

    {
        const uint8_t bytes[] =
            "one two three four five six seven eight nine ten\r"
            "eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty\r"
            " STOP HERE\r"
            "tail";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    editor.justify_mode = 0;
    editor.cursor_line = 0u;
    editor.cursor_column = 4u;
    CHECK(viewbbc_markers_set(&editor.markers, 3u, 3u, 2u));
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_F0);
    CHECK(editor.file_modified == 1);
    CHECK(editor.cursor_line == 0u && editor.cursor_column == 0u);
    CHECK(viewbbc_document_line_count(&editor.document) == 4u);
    CHECK(viewbbc_document_line_length(&editor.document, 0u) <= VIEWBBC_RULER_WIDTH);
    CHECK(viewbbc_document_line_length(&editor.document, 1u) <= VIEWBBC_RULER_WIDTH);
    CHECK(viewbbc_document_char_at(&editor.document, 2u, 0u) == ' ');
    CHECK(viewbbc_document_line_length(&editor.document, 2u) == 10u);
    CHECK(memcmp(editor.document.lines[2].data, " STOP HERE", 10u) == 0);
    CHECK(memcmp(editor.document.lines[3].data, "tail", 4u) == 0);
    {
        size_t line = 0, column = 0;
        CHECK(viewbbc_markers_get(&editor.markers, 3u, &line, &column));
        CHECK(line == 3u && column == 2u);
    }
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] =
            "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau\r"
            "upsilon phi chi psi omega";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    editor.justify_mode = 1;
    editor.cursor_line = 0u;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_F0);
    CHECK(viewbbc_document_line_count(&editor.document) >= 2u);
    CHECK(viewbbc_document_line_length(&editor.document, 0u) == VIEWBBC_RULER_WIDTH);
    CHECK(viewbbc_document_line_length(&editor.document,
                                       viewbbc_document_line_count(&editor.document) - 1u) <= VIEWBBC_RULER_WIDTH);
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] = "one two three\rfour five six";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    editor.format_mode = 0;
    editor.cursor_line = 0u;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_F0);
    CHECK(strstr(editor.status_message, "Format mode off") != NULL);
    CHECK(viewbbc_document_line_count(&editor.document) == 2u);
    CHECK(memcmp(editor.document.lines[0].data, "one two three", 13u) == 0);
    CHECK(editor.file_modified == 0);
    viewbbc_editor_destroy(&editor);
}

static void test_block_operations(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;

    const char *text = "alpha beta gamma";
    for (const char *p = text; *p; ++p) viewbbc_editor_insert_char(&editor, (unsigned char)*p);
    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 6u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 0u, 11u));
    editor.cursor_column = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F0);
    CHECK(viewbbc_document_line_length(&editor.document, 0) == 11u);
    CHECK(viewbbc_document_char_at(&editor.document, 0, 6) == 'g');
    CHECK(viewbbc_document_char_at(&editor.document, 0, 7) == 'a');
    CHECK(editor.cursor_line == 0u && editor.cursor_column == 6u);
    CHECK(editor.file_modified == 1);
    {
        size_t line = 0, column = 0;
        CHECK(!viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(!viewbbc_markers_get(&editor.markers, 2u, &line, &column));
    }
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] = "one\rTWO\rthree";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 3u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 2u, 0u));
    editor.cursor_line = 2u;
    editor.cursor_column = 5u;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F0);
    CHECK(viewbbc_document_line_count(&editor.document) == 1u);
    CHECK(viewbbc_document_line_length(&editor.document, 0u) == 8u);
    CHECK(memcmp(editor.document.lines[0].data, "onethree", 8u) == 0);
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] = "AAA\rBBB\rCCC";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    CHECK(viewbbc_markers_set(&editor.markers, 1u, 1u, 0u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 1u, 3u));
    editor.cursor_line = 0u;
    editor.cursor_column = 0u;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F0);
    CHECK(viewbbc_document_line_count(&editor.document) == 3u);
    CHECK(memcmp(editor.document.lines[0].data, "BBBAAA", 6u) == 0);
    CHECK(viewbbc_document_line_length(&editor.document, 1u) == 0u);
    CHECK(memcmp(editor.document.lines[2].data, "CCC", 3u) == 0);
    CHECK(editor.cursor_line == 0u && editor.cursor_column == 0u);
    {
        size_t line = 0, column = 0;
        CHECK(viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(line == 0u && column == 0u);
        CHECK(viewbbc_markers_get(&editor.markers, 2u, &line, &column));
        CHECK(line == 0u && column == 3u);
    }
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] = "abcdef";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 1u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 0u, 4u));
    editor.cursor_column = 2u;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F0);
    CHECK(strstr(editor.status_message, "inside block") != NULL);
    CHECK(memcmp(editor.document.lines[0].data, "abcdef", 6u) == 0);

    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 5u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 0u, 2u));
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F0);
    CHECK(strstr(editor.status_message, "invalid") != NULL);
    CHECK(memcmp(editor.document.lines[0].data, "abcdef", 6u) == 0);

    viewbbc_markers_clear(&editor.markers);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F0);
    CHECK(strstr(editor.status_message, "not set") != NULL);
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] = "one two";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 4u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 0u, 7u));
    editor.cursor_line = 0u;
    editor.cursor_column = 0u;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_COPY);
    CHECK(viewbbc_document_line_length(&editor.document, 0u) == 10u);
    CHECK(memcmp(editor.document.lines[0].data, "twoone two", 10u) == 0);
    CHECK(editor.cursor_line == 0u && editor.cursor_column == 3u);
    {
        size_t line = 0, column = 0;
        CHECK(viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(line == 0u && column == 7u);
        CHECK(viewbbc_markers_get(&editor.markers, 2u, &line, &column));
        CHECK(line == 0u && column == 10u);
    }
    editor.cursor_column = 8u;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_COPY);
    CHECK(strstr(editor.status_message, "inside block") != NULL);
    viewbbc_editor_destroy(&editor);
}


static void test_marker_aware_search_and_count(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    {
        const uint8_t bytes[] = "one alpha one beta one";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 4u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 0u, 19u));

    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "COUNT 1 2");
    CHECK(editor.command_output.line_count == 1u);
    CHECK(strcmp(editor.command_output.lines[0], "Words: 3") == 0);

    editor_command(&editor, "SEARCH one 1 2");
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);
    CHECK(editor.cursor_line == 0u && editor.cursor_column == 10u);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F1);
    CHECK(strstr(editor.status_message, "Not found") != NULL);
    CHECK(editor.cursor_column == 10u);

    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "SEARCH \"alpha one\" 1 2");
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);
    CHECK(editor.cursor_column == 4u);

    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "SEARCH one");
    CHECK(editor.mode == VIEWBBC_MODE_TEXT && editor.cursor_column == 0u);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F1);
    CHECK(editor.cursor_column == 10u);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_CTRL_F1);
    CHECK(editor.cursor_column == 19u);

    viewbbc_editor_destroy(&editor);
}


static void test_change_replace_fold(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    {
        const uint8_t bytes[] = "London london LONDON aspirin london";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    CHECK(viewbbc_markers_set(&editor.markers, 1u, 0u, 7u));
    CHECK(viewbbc_markers_set(&editor.markers, 2u, 0u, 20u));

    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "CHANGE london glasgow 1 2");
    CHECK(memcmp(editor.document.lines[0].data, "London glasgow GLASGOW aspirin london", 37u) == 0);

    editor_command(&editor, "FOLD 0");
    CHECK(editor.fold_mode == 0);
    editor_command(&editor, "CHANGE aspirin Aspirin");
    CHECK(editor.document.lines[0].length == 37u);
    CHECK(memcmp(editor.document.lines[0].data, "London glasgow GLASGOW Aspirin london", 37u) == 0);

    editor_command(&editor, "FOLD 1");
    CHECK(editor.fold_mode == 1);
    editor_command(&editor, "REPLACE london bristol");
    CHECK(editor.replace_active);
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);
    CHECK(editor.cursor_column == 0u);
    viewbbc_editor_insert_char(&editor, 'N');
    CHECK(editor.replace_active);
    CHECK(editor.cursor_column > 0u);
    viewbbc_editor_insert_char(&editor, 'Y');
    CHECK(!editor.replace_active);
    CHECK(editor.mode == VIEWBBC_MODE_COMMAND);
    CHECK(editor.document.lines[0].length == 38u);
    CHECK(memcmp(editor.document.lines[0].data, "London glasgow GLASGOW Aspirin bristol", 38u) == 0);

    editor_command(&editor, "REPLACE bristol york");
    CHECK(editor.replace_active);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_ESCAPE);
    CHECK(!editor.replace_active);
    CHECK(editor.mode == VIEWBBC_MODE_COMMAND);

    viewbbc_editor_destroy(&editor);
}


static void test_global_format_and_immediate_editing(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    {
        const char *body = "one two three four five six seven eight nine ten eleven twelve thirteen fourteen fifteen sixteen seventeen eighteen nineteen twenty";
        char text[256];
        int n = snprintf(text, sizeof(text), "HEAD\r%s\rTAIL", body);
        CHECK(n > 0 && (size_t)n < sizeof(text));
        CHECK(viewbbc_document_load_bytes(&editor.document, (const uint8_t *)text, (size_t)n));
        CHECK(viewbbc_markers_set(&editor.markers, 1u, 1u, 0u));
        CHECK(viewbbc_markers_set(&editor.markers, 2u, 1u, strlen(body)));
    }
    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "FORMAT 1 2");
    CHECK(strcmp(editor.status_message, "Formatted") == 0);
    CHECK(viewbbc_document_line_count(&editor.document) == 4u);
    CHECK(viewbbc_document_line_length(&editor.document, 0u) == 4u);
    CHECK(memcmp(editor.document.lines[0].data, "HEAD", 4u) == 0);
    CHECK(viewbbc_document_line_length(&editor.document, 1u) == VIEWBBC_RULER_WIDTH);
    CHECK(viewbbc_document_line_length(&editor.document, 3u) == 4u);
    CHECK(memcmp(editor.document.lines[3].data, "TAIL", 4u) == 0);
    {
        size_t line = 0, column = 0;
        CHECK(viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(line == 1u && column == 0u);
        CHECK(viewbbc_markers_get(&editor.markers, 2u, &line, &column));
        CHECK(line == 2u);
    }
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] = "aB1";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F1);
    CHECK(viewbbc_document_char_at(&editor.document, 0u, 0u) == 'A');
    CHECK(editor.cursor_column == 1u);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F1);
    CHECK(viewbbc_document_char_at(&editor.document, 0u, 1u) == 'b');
    CHECK(editor.cursor_column == 2u);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F1);
    CHECK(viewbbc_document_char_at(&editor.document, 0u, 2u) == '1');
    CHECK(editor.cursor_column == 3u);
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] = "abcdefxxghi";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    editor.cursor_column = 2u;
    CHECK(viewbbc_markers_set(&editor.markers, 3u, 0u, 10u));
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F3);
    CHECK(editor.delete_to_char_pending);
    viewbbc_editor_insert_char(&editor, 'x');
    CHECK(!editor.delete_to_char_pending);
    CHECK(viewbbc_document_line_length(&editor.document, 0u) == 5u);
    CHECK(memcmp(editor.document.lines[0].data, "abghi", 5u) == 0);
    {
        size_t line = 0, column = 0;
        CHECK(viewbbc_markers_get(&editor.markers, 3u, &line, &column));
        CHECK(line == 0u && column == 4u);
    }
    viewbbc_editor_destroy(&editor);
}


static void test_mouse_operations(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        const uint8_t bytes[] = "abcdef";
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, sizeof(bytes) - 1u));
    }
    viewbbc_editor_render(&editor);

    /* A simple click moves the cursor but does not consume markers 1/2. */
    viewbbc_editor_mouse_drag_begin(&editor, 5, 1); /* document column 2 */
    viewbbc_editor_mouse_drag_end(&editor, 5, 1);
    CHECK(editor.cursor_line == 0u && editor.cursor_column == 2u);
    {
        size_t line = 0, column = 0;
        CHECK(!viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(!viewbbc_markers_get(&editor.markers, 2u, &line, &column));
    }

    /* Drag selection is cell-inclusive and becomes VIEW markers 1/2. */
    viewbbc_editor_mouse_drag_begin(&editor, 4, 1); /* b */
    viewbbc_editor_mouse_drag_update(&editor, 6, 1); /* d */
    viewbbc_editor_mouse_drag_end(&editor, 6, 1);
    {
        size_t line = 0, column = 0;
        CHECK(viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(line == 0u && column == 1u);
        CHECK(viewbbc_markers_get(&editor.markers, 2u, &line, &column));
        CHECK(line == 0u && column == 4u);
    }
    CHECK(editor.mouse_selection_active);
    viewbbc_editor_render(&editor);
    CHECK((viewbbc_screen_get(&editor.screen, 4, 1).attr & VIEWBBC_ATTR_REVERSE) != 0);
    CHECK((viewbbc_screen_get(&editor.screen, 5, 1).attr & VIEWBBC_ATTR_REVERSE) != 0);
    CHECK((viewbbc_screen_get(&editor.screen, 6, 1).attr & VIEWBBC_ATTR_REVERSE) != 0);
    CHECK((viewbbc_screen_get(&editor.screen, 7, 1).attr & VIEWBBC_ATTR_REVERSE) == 0);

    /* Double/triple-click helpers and Select All use the same markers 1/2. */
    {
        const uint8_t words[] = "one two three";
        CHECK(viewbbc_document_load_bytes(&editor.document, words, sizeof(words) - 1u));
        viewbbc_editor_mouse_select_word(&editor, 8, 1); /* 't' in two */
        size_t line = 99u, column = 99u;
        CHECK(viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(line == 0u && column == 4u);
        CHECK(viewbbc_markers_get(&editor.markers, 2u, &line, &column));
        CHECK(line == 0u && column == 7u);
        viewbbc_editor_mouse_select_line(&editor, 5, 1);
        CHECK(viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(line == 0u && column == 0u);
        CHECK(viewbbc_markers_get(&editor.markers, 2u, &line, &column));
        CHECK(line == 0u && column == sizeof(words) - 1u);
    }
    {
        const uint8_t multi[] = "abc\rdef";
        CHECK(viewbbc_document_load_bytes(&editor.document, multi, sizeof(multi) - 1u));
        viewbbc_editor_select_all(&editor);
        size_t line = 99u, column = 99u;
        CHECK(editor.mouse_selection_active);
        CHECK(viewbbc_markers_get(&editor.markers, 1u, &line, &column));
        CHECK(line == 0u && column == 0u);
        CHECK(viewbbc_markers_get(&editor.markers, 2u, &line, &column));
        CHECK(line == 1u && column == 3u);
    }
    viewbbc_editor_destroy(&editor);

    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    {
        uint8_t bytes[80];
        size_t n = 0u;
        for (unsigned i = 0u; i < 40u; ++i) {
            bytes[n++] = 'x';
            if (i + 1u < 40u) bytes[n++] = '\r';
        }
        CHECK(viewbbc_document_load_bytes(&editor.document, bytes, n));
    }
    editor.cursor_line = 10u;
    editor.viewport_top = 5u;
    viewbbc_editor_mouse_wheel(&editor, -3);
    CHECK(editor.cursor_line == 13u);
    CHECK(editor.viewport_top == 8u);
    viewbbc_editor_mouse_wheel(&editor, 3);
    CHECK(editor.cursor_line == 10u);
    CHECK(editor.viewport_top == 5u);
    viewbbc_editor_destroy(&editor);
}

static void test_inline_exit_confirmation(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));

    editor.mode = VIEWBBC_MODE_TEXT;
    viewbbc_editor_insert_char(&editor, 'X');
    CHECK(editor.file_modified == 1);
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_QUIT);
    CHECK(editor.mode == VIEWBBC_MODE_COMMAND);
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_EXIT);
    CHECK(editor.running == 1);

    char row[128];
    screen_row_text(&editor.screen, 8, row, sizeof(row));
    CHECK(strcmp(row, "File modified - exit without saving? (Y/N)") == 0);
    CHECK(editor.screen.cursor_y == 8);
    CHECK(editor.screen.cursor_x == (int)strlen("File modified - exit without saving? (Y/N) "));

    viewbbc_editor_insert_char(&editor, 'N');
    CHECK(editor.running == 1);
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_NONE);
    screen_row_text(&editor.screen, 8, row, sizeof(row));
    CHECK(strcmp(row, "File modified - exit without saving? (Y/N) No") == 0);
    screen_row_text(&editor.screen, 9, row, sizeof(row));
    CHECK(strcmp(row, "=>") == 0);

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_QUIT);
    CHECK(editor.confirm_action == VIEWBBC_CONFIRM_EXIT);
    viewbbc_editor_insert_char(&editor, 'Y');
    CHECK(editor.running == 0);
    viewbbc_editor_destroy(&editor);
}

static void test_print_export(void) {
    ViewBBCDocument doc;
    CHECK(viewbbc_document_init(&doc));
    {
        const uint8_t bytes[] = "first\rsecond";
        CHECK(viewbbc_document_load_bytes(&doc, bytes, sizeof(bytes) - 1u));
    }
    char path[256];
    test_temp_path(path, sizeof(path), "viewbbc-print", "txt");
    char target[300];
    (void)snprintf(target, sizeof(target), "file:%s", path);
    char message[256];
    CHECK(viewbbc_print_document(&doc, target, message, sizeof(message)));
    FILE *in = fopen(path, "rb");
    CHECK(in != NULL);
    char data[64] = {0};
    size_t got = fread(data, 1u, sizeof(data) - 1u, in);
    CHECK(fclose(in) == 0);
    CHECK(got == strlen("first\nsecond"));
    CHECK(strcmp(data, "first\nsecond") == 0);
    CHECK(unlink(path) == 0);

    test_temp_path(path, sizeof(path), "viewbbc-print", "pdf");
    (void)snprintf(target, sizeof(target), "pdf:%s", path);
    CHECK(viewbbc_print_document(&doc, target, message, sizeof(message)));
    in = fopen(path, "rb"); CHECK(in != NULL); memset(data, 0, sizeof(data)); got = fread(data, 1u, 8u, in); CHECK(fclose(in) == 0);
    CHECK(got == 8u); CHECK(strncmp(data, "%PDF-1.4", 8u) == 0); CHECK(unlink(path) == 0);

    test_temp_path(path, sizeof(path), "viewbbc-print", "odt");
    (void)snprintf(target, sizeof(target), "odt:%s", path);
    CHECK(viewbbc_print_document(&doc, target, message, sizeof(message)));
    in = fopen(path, "rb"); CHECK(in != NULL); memset(data, 0, sizeof(data)); got = fread(data, 1u, 4u, in); CHECK(fclose(in) == 0);
    CHECK(got == 4u); CHECK((unsigned char)data[0] == 'P' && (unsigned char)data[1] == 'K'); CHECK(unlink(path) == 0);

    /* A directory is not a valid print-output file on either POSIX or Windows. */
    (void)snprintf(target, sizeof(target), "text:%s", test_temp_dir());
    CHECK(!viewbbc_print_document(&doc, target, message, sizeof(message)));
    CHECK(strstr(message, "Cannot open output file") != NULL || strstr(message, "Write failed") != NULL);
    viewbbc_document_destroy(&doc);

    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor_command(&editor, "PRINT file:test.txt");
    CHECK(editor.print_requested);
    CHECK(strcmp(editor.print_target_override, "file:test.txt") == 0);
    editor.print_requested = 0;
    editor_command(&editor, "EXPORT export.txt");
    CHECK(editor.print_requested);
    CHECK(strcmp(editor.print_target_override, "file:export.txt") == 0);
    viewbbc_editor_destroy(&editor);
}

static void test_config_defaults_and_parser(void) {
    ViewBBCConfig config;
    ViewBBCPhysicalKey physical;
    viewbbc_config_defaults(&config);
    CHECK(config.font_size == 20);
    CHECK(strcmp(config.font, "mode7") == 0);
    CHECK(strcmp(config.print_target, "printer:default") == 0);
    CHECK(config.mouse_buttons[1] == VIEWBBC_MOUSE_SELECT);
    CHECK(config.mouse_buttons[2] == VIEWBBC_MOUSE_NONE);
    CHECK(config.mouse_wheel_lines == 3);
    CHECK(config.buffer_size == VIEWBBC_DEFAULT_WORKSPACE_LIMIT);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+F10", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_CTRL_F0);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+Up", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_PAGE_UP);
    CHECK(viewbbc_config_parse_physical_key("Insert", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_CTRL_F4);
    CHECK(config.key_binding_count[VIEWBBC_KEY_CTRL_F4] == 2);
    CHECK(viewbbc_config_parse_physical_key("Left", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_LEFT);
    CHECK(viewbbc_config_key_setting_name(VIEWBBC_KEY_LEFT) == NULL);
    CHECK(viewbbc_config_key_setting_name(VIEWBBC_KEY_ESCAPE) == NULL);
    CHECK(viewbbc_config_parse_physical_key("F11", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_COPY);
    CHECK(viewbbc_config_parse_physical_key("F12", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_NONE);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+F", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_FIND);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+H", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_FIND_REPLACE);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+V", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_CLIPBOARD_PASTE);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+C", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_CLIPBOARD_COPY);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+X", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_CLIPBOARD_CUT);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+A", &physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_SELECT_ALL);
    CHECK(viewbbc_config_parse_physical_key("Ctrl+1", &physical));
    CHECK(viewbbc_config_physical_key_allowed(physical));
    CHECK(viewbbc_config_set_key_binding(&config, VIEWBBC_KEY_SHIFT_F2, physical));
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_SHIFT_F2);
    CHECK(viewbbc_config_parse_physical_key("Alt+F2", &physical));
    CHECK(viewbbc_config_add_key_binding(&config, VIEWBBC_KEY_SHIFT_F2, physical));
    CHECK(config.key_binding_count[VIEWBBC_KEY_SHIFT_F2] == 2);
    CHECK(viewbbc_config_key_for_physical(&config, physical) == VIEWBBC_KEY_SHIFT_F2);
    CHECK(viewbbc_config_parse_physical_key("1", &physical));
    CHECK(!viewbbc_config_physical_key_allowed(physical));
    CHECK(viewbbc_config_parse_physical_key("Escape", &physical));
    CHECK(!viewbbc_config_physical_key_allowed(physical));
    CHECK(viewbbc_config_parse_physical_key("Up", &physical));
    CHECK(!viewbbc_config_physical_key_allowed(physical));

    {
        ViewBBCEditor editor;
        CHECK(viewbbc_editor_init(&editor, 80, 25));
        editor.mode = VIEWBBC_MODE_TEXT;
        viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_FIND);
        CHECK(editor.mode == VIEWBBC_MODE_COMMAND);
        CHECK(strcmp(editor.command_buffer, "SEARCH ") == 0);
        viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_FIND_REPLACE);
        CHECK(strcmp(editor.command_buffer, "CHANGE ") == 0);
        editor_command(&editor, "CONFIG");
        CHECK(editor.config_mode_requested);
        editor.config_mode_requested = 0;
        editor_command(&editor, "CONFIG RESET");
        CHECK(editor.confirm_action == VIEWBBC_CONFIRM_CONFIG_RESET);
        viewbbc_editor_insert_char(&editor, 'Y');
        CHECK(editor.config_reset_requested);
        viewbbc_editor_destroy(&editor);
    }
}



static void test_command_line_editing_and_completion(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_COMMAND;

    viewbbc_editor_insert_char(&editor, 'A');
    viewbbc_editor_insert_char(&editor, 'B');
    viewbbc_editor_insert_char(&editor, 'C');
    CHECK(strcmp(editor.command_buffer, "ABC") == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_LEFT);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_LEFT);
    viewbbc_editor_insert_char(&editor, 'X');
    CHECK(strcmp(editor.command_buffer, "AXBC") == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_DELETE);
    CHECK(strcmp(editor.command_buffer, "AXC") == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_BACKSPACE);
    CHECK(strcmp(editor.command_buffer, "AC") == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_HOME);
    CHECK(editor.command_cursor == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_END);
    CHECK(editor.command_cursor == editor.command_length);

    memcpy(editor.command_buffer, "SETUP F", 8);
    editor.command_length = 7;
    editor.command_cursor = 7;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_RETURN);
    memcpy(editor.command_buffer, "SETUP J", 8);
    editor.command_length = 7;
    editor.command_cursor = 7;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_RETURN);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_UP);
    CHECK(strcmp(editor.command_buffer, "SETUP J") == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_UP);
    CHECK(strcmp(editor.command_buffer, "SETUP F") == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_DOWN);
    CHECK(strcmp(editor.command_buffer, "SETUP J") == 0);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_DOWN);
    CHECK(strcmp(editor.command_buffer, "") == 0);

    char template[512];
    CHECK(test_make_temp_dir(template, sizeof(template), "viewbbc-complete"));
    char *dir = template;
    char path[512];
    FILE *fp;
    snprintf(path, sizeof(path), "%s/alpha.txt", dir);
    fp = fopen(path, "wb"); CHECK(fp != NULL); fclose(fp);
    snprintf(path, sizeof(path), "%s/alphabet.txt", dir);
    fp = fopen(path, "wb"); CHECK(fp != NULL); fclose(fp);
    snprintf(path, sizeof(path), "%s/unique.dat", dir);
    fp = fopen(path, "wb"); CHECK(fp != NULL); fclose(fp);
    snprintf(path, sizeof(path), "%s/my file.txt", dir);
    fp = fopen(path, "wb"); CHECK(fp != NULL); fclose(fp);
    snprintf(path, sizeof(path), "%s/folder", dir);
    CHECK(test_mkdir(path) == 0);
    char error[128];
    CHECK(viewbbc_fs_chdir_native(&editor.filesystem, dir, error, sizeof(error)));

    size_t before_lines = 0u;

    memcpy(editor.command_buffer, "buf", 4);
    editor.command_length = 3;
    editor.command_cursor = 3;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "BUFFERSIZE ") == 0);

    memcpy(editor.command_buffer, "f", 2);
    editor.command_length = 1;
    editor.command_cursor = 1;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "FO") == 0);
    CHECK(editor.command_completion_pending);
    before_lines = editor.console.count;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(editor.console.count >= before_lines + 2u);

    memcpy(editor.command_buffer, "loa", 4);
    editor.command_length = 3;
    editor.command_cursor = 3;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "LOAD ") == 0);

    memcpy(editor.command_buffer, "CONFIG r", 9);
    editor.command_length = 8;
    editor.command_cursor = 8;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "CONFIG RESET ") == 0);

    memcpy(editor.command_buffer, "CONFIG l", 9);
    editor.command_length = 8;
    editor.command_cursor = 8;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "CONFIG LOC ") == 0);

    memcpy(editor.command_buffer, "LOAD al", 8);
    editor.command_length = 7;
    editor.command_cursor = 7;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "LOAD alpha") == 0);
    CHECK(editor.command_completion_pending);
    before_lines = editor.console.count;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(editor.console.count >= before_lines + 2u);

    memcpy(editor.command_buffer, "LOAD uni", 9);
    editor.command_length = 8;
    editor.command_cursor = 8;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "LOAD unique.dat ") == 0);

    memcpy(editor.command_buffer, "CD fol", 7);
    editor.command_length = 6;
    editor.command_cursor = 6;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "CD folder/") == 0);

    memcpy(editor.command_buffer, "LOAD \"my f", 11);
    editor.command_length = 10;
    editor.command_cursor = 10;
    editor.command_completion_pending = 0;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_TAB);
    CHECK(strcmp(editor.command_buffer, "LOAD \"my file.txt\" ") == 0);

    snprintf(path, sizeof(path), "%s/alpha.txt", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/alphabet.txt", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/unique.dat", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/my file.txt", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/folder", dir); rmdir(path);
    rmdir(dir);
    viewbbc_editor_destroy(&editor);
}

static void test_setup_command(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_COMMAND;

    editor_command(&editor, "SETUP F");
    CHECK(editor.format_mode == 1 && editor.justify_mode == 0 && editor.insert_mode == 0);
    editor_command(&editor, "SETUP KW");
    CHECK(editor.format_mode == 0 && editor.justify_mode == 1 && editor.insert_mode == 1);
    editor_command(&editor, "SETUP FKW");
    CHECK(editor.format_mode == 1 && editor.justify_mode == 1 && editor.insert_mode == 1);
    editor_command(&editor, "SETUP X");
    CHECK(strstr(editor.status_message, "F, K and W") != NULL);
    CHECK(editor.format_mode == 1 && editor.justify_mode == 1 && editor.insert_mode == 1);
    editor_command(&editor, "SETUP J"); /* compatibility alias for older BeebView configs/scripts */
    CHECK(editor.justify_mode == 1);

    editor_command(&editor, "SETUP FKW");
    editor_command(&editor, "SETUP");
    CHECK(strstr(editor.status_message, "FKW") != NULL);
    viewbbc_editor_destroy(&editor);
}


static void test_buffersize_command_and_config(void) {
    size_t bytes = 0;
    char formatted[48];
    CHECK(viewbbc_workspace_parse_size("65536", &bytes) && bytes == 65536u);
    CHECK(viewbbc_workspace_parse_size("100 KB", &bytes) && bytes == 100u * 1024u);
    CHECK(viewbbc_workspace_parse_size("10MB", &bytes) && bytes == 10u * 1024u * 1024u);
    CHECK(viewbbc_workspace_parse_size("10 mb", &bytes) && bytes == 10u * 1024u * 1024u);
    CHECK(!viewbbc_workspace_parse_size("10 GB", &bytes));
    CHECK(!viewbbc_workspace_parse_size("1.5MB", &bytes));
    viewbbc_workspace_format_size(65536u, formatted, sizeof(formatted));
    CHECK(strcmp(formatted, "64 KB") == 0);

    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_COMMAND;
    editor_command(&editor, "BUFFERSIZE");
    CHECK(strstr(editor.status_message, "1 MB") != NULL);
    editor_command(&editor, "BUFFERSIZE 100 KB");
    CHECK(viewbbc_document_workspace_limit(&editor.document) == 100u * 1024u);
    CHECK(editor.buffer_size_changed);
    CHECK(editor.buffer_size_requested == 100u * 1024u);
    editor.buffer_size_changed = 0;
    editor_command(&editor, "BUFFERSIZE 65536");
    CHECK(viewbbc_document_workspace_limit(&editor.document) == 65536u);
    editor_command(&editor, "BUFFERSIZE 31KB");
    CHECK(viewbbc_document_workspace_limit(&editor.document) == 65536u);
    CHECK(strstr(editor.status_message, "Minimum") != NULL);
    editor_command(&editor, "BUFFERSIZE 101MB");
    CHECK(viewbbc_document_workspace_limit(&editor.document) == 65536u);
    CHECK(strstr(editor.status_message, "Maximum") != NULL);
    viewbbc_editor_destroy(&editor);

    char path[512];
    int fd = test_make_temp_file(path, sizeof(path), "viewbbc-config-buffer");
    CHECK(fd >= 0);
    close(fd);
    ViewBBCConfig config;
    viewbbc_config_defaults(&config);
    config.buffer_size = 10u * 1024u * 1024u;
    CHECK(viewbbc_config_write_defaults(&config, path));
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    char all[16384];
    size_t got = fread(all, 1u, sizeof(all) - 1u, f);
    CHECK(fclose(f) == 0);
    all[got] = '\0';
    CHECK(strstr(all, "buffersize = 10 MB") != NULL);
    ViewBBCConfig loaded;
    viewbbc_config_defaults(&loaded);
    CHECK(viewbbc_config_load_file(&loaded, path));
    CHECK(loaded.buffer_size == 10u * 1024u * 1024u);
    CHECK(unlink(path) == 0);
}


static void test_version_and_line_numbers(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    CHECK(viewbbc_editor_text_x(&editor) == 3);

    editor_command(&editor, "VER");
    CHECK(editor.command_output.line_count == 1u);
    CHECK(strcmp(editor.command_output.lines[0], VIEWBBC_PRODUCT_NAME " " VIEWBBC_VERSION_STRING) == 0);

    editor_command(&editor, "ROWCOLS");
    CHECK(editor.command_output.line_count == 1u);
    CHECK(strcmp(editor.command_output.lines[0], "Row/column display: OFF") == 0);
    editor_command(&editor, "ROWCOLS ON");
    CHECK(editor.rowcols == 1);
    CHECK(editor.rowcols_changed == 1);
    CHECK(editor.rowcols_requested == 1);

    (void)snprintf(editor.config_path, sizeof(editor.config_path), "%s", "/tmp/example/viewbeeb.conf");
    editor_command(&editor, "CONFIG LOC");
    CHECK(editor.command_output.line_count == 1u);
    CHECK(strcmp(editor.command_output.lines[0], "Configuration: /tmp/example/viewbeeb.conf") == 0);

    editor_command(&editor, "LINENUMS");
    CHECK(editor.command_output.line_count == 1u);
    CHECK(strcmp(editor.command_output.lines[0], "Line numbers: OFF") == 0);

    editor_command(&editor, "LINENUMS ON");
    CHECK(editor.line_numbers == 1);
    CHECK(editor.line_numbers_changed == 1);
    CHECK(editor.line_numbers_requested == 1);

    uint8_t data[300];
    size_t n = 0u;
    for (size_t i = 0; i < 100u; ++i) {
        data[n++] = 'X';
        if (i + 1u < 100u) data[n++] = '\r';
    }
    CHECK(viewbbc_document_load_bytes(&editor.document, data, n));
    editor.mode = VIEWBBC_MODE_TEXT;
    editor.cursor_line = 0u;
    editor.cursor_column = 0u;
    editor.viewport_top = 0u;
    editor.viewport_left = 0u;
    viewbbc_editor_render(&editor);

    CHECK(viewbbc_editor_text_x(&editor) == 7); /* 3-column VIEW margin + 3 digits + space */
    CHECK(viewbbc_screen_get(&editor.screen, 73, 0).ch == 'R');
    CHECK(viewbbc_screen_get(&editor.screen, 79, 0).ch == '1');
    CHECK(viewbbc_screen_get(&editor.screen, 3, 1).ch == ' ');
    CHECK(viewbbc_screen_get(&editor.screen, 4, 1).ch == ' ');
    CHECK(viewbbc_screen_get(&editor.screen, 5, 1).ch == '1');
    CHECK(viewbbc_screen_get(&editor.screen, 7, 1).ch == 'X');

    /* Clicking anywhere in the display gutter moves to the start of that line. */
    viewbbc_editor_mouse_click(&editor, 4, 2);
    CHECK(editor.cursor_line == 1u);
    CHECK(editor.cursor_column == 0u);

    viewbbc_editor_set_line_numbers(&editor, 0);
    CHECK(viewbbc_editor_text_x(&editor) == 3);
    viewbbc_editor_render(&editor);
    CHECK(viewbbc_screen_get(&editor.screen, 3, 1).ch == 'X');
    viewbbc_editor_destroy(&editor);

    char path[512];
    int fd = test_make_temp_file(path, sizeof(path), "viewbbc-config-linenums");
    CHECK(fd >= 0);
    close(fd);
    ViewBBCConfig config;
    viewbbc_config_defaults(&config);
    CHECK(config.line_numbers == 0);
    CHECK(config.rowcols == 0);
    config.line_numbers = 1;
    config.rowcols = 1;
    CHECK(viewbbc_config_write_defaults(&config, path));
    FILE *f = fopen(path, "rb");
    CHECK(f != NULL);
    char all[16384];
    size_t got = fread(all, 1u, sizeof(all) - 1u, f);
    CHECK(fclose(f) == 0);
    all[got] = '\0';
    CHECK(strstr(all, "linenums = on") != NULL);
    CHECK(strstr(all, "rowcols = on") != NULL);
    ViewBBCConfig loaded;
    viewbbc_config_defaults(&loaded);
    CHECK(viewbbc_config_load_file(&loaded, path));
    CHECK(loaded.line_numbers == 1);
    CHECK(loaded.rowcols == 1);
    CHECK(unlink(path) == 0);
}

static void test_startup_arguments(void) {
    ViewBBCStartupOptions options;
    char error[256];

    char *argv0[] = { (char *)"viewbbc" };
    CHECK(viewbbc_startup_parse(1, argv0, &options, error, sizeof(error)));
    CHECK(!options.has_mount && !options.has_file && !options.show_help);

    char *argv1[] = { (char *)"viewbbc", (char *)"notes file.txt" };
    CHECK(viewbbc_startup_parse(2, argv1, &options, error, sizeof(error)));
    CHECK(!options.has_mount && options.has_file);
    CHECK(strcmp(options.file_path, "notes file.txt") == 0);

    char *argv2[] = { (char *)"viewbbc", (char *)"mount:ssd:My Disc.img", (char *)"LETTER" };
    CHECK(viewbbc_startup_parse(3, argv2, &options, error, sizeof(error)));
    CHECK(options.has_mount && options.has_file);
    CHECK(options.mount_type == VIEWBBC_DFS_TYPE_SSD);
    CHECK(strcmp(options.mount_path, "My Disc.img") == 0);
    CHECK(strcmp(options.file_path, "LETTER") == 0);

    char *argv3[] = { (char *)"viewbbc", (char *)"MOUNT:\"My Disc.ssd\"", (char *)"\"LETTER\"" };
    CHECK(viewbbc_startup_parse(3, argv3, &options, error, sizeof(error)));
    CHECK(strcmp(options.mount_path, "My Disc.ssd") == 0);
    CHECK(strcmp(options.file_path, "LETTER") == 0);

    char *argv4[] = { (char *)"viewbbc", (char *)"file", (char *)"mount:disc.ssd" };
    CHECK(!viewbbc_startup_parse(3, argv4, &options, error, sizeof(error)));
    CHECK(strstr(error, "before") != NULL);

    char *argv5[] = { (char *)"viewbbc", (char *)"one", (char *)"two" };
    CHECK(!viewbbc_startup_parse(3, argv5, &options, error, sizeof(error)));

    char *argv6[] = { (char *)"viewbbc", (char *)"--help" };
    CHECK(viewbbc_startup_parse(2, argv6, &options, error, sizeof(error)));
    CHECK(options.show_help);

    char template[512];
    int fd = test_make_temp_file(template, sizeof(template), "viewbbc-startup");
    CHECK(fd >= 0);
    const char contents[] = "alpha\rbeta";
    CHECK(write(fd, contents, sizeof(contents) - 1u) == (ssize_t)(sizeof(contents) - 1u));
    close(fd);

    char *argv7[] = { (char *)"viewbbc", template };
    CHECK(viewbbc_startup_parse(2, argv7, &options, error, sizeof(error)));
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    CHECK(viewbbc_startup_apply(&editor, &options));
    CHECK(editor.current_file != NULL);
    CHECK(viewbbc_document_line_count(&editor.document) == 2u);
    CHECK(viewbbc_document_line_length(&editor.document, 0) == 5u);
    CHECK(viewbbc_document_char_at(&editor.document, 1, 0) == 'b');
    CHECK(editor.mode == VIEWBBC_MODE_TEXT);
    viewbbc_editor_destroy(&editor);
    unlink(template);

    char *argv8[] = { (char *)"viewbbc", (char *)"--mountlist:dsd:archive.img" };
    CHECK(viewbbc_startup_parse(2, argv8, &options, error, sizeof(error)));
    CHECK(options.show_mountlist);
    CHECK(options.mount_type == VIEWBBC_DFS_TYPE_DSD);
    CHECK(strcmp(options.mount_path, "archive.img") == 0);

    char list_template[512];
    int list_fd = test_make_temp_file(list_template, sizeof(list_template), "viewbbc-mountlist");
    CHECK(list_fd >= 0);
    uint8_t list_image[80u * 10u * 256u] = {0};
    memcpy(list_image, "LISTDISC", 8);
    memcpy(list_image + 8, "HELLO  ", 7);
    list_image[15] = '$';
    uint8_t *list_info = list_image + 256;
    list_info[5] = 8;
    list_info[6] = 3;
    list_info[7] = 32;
    list_info[8 + 4] = 1;
    list_info[8 + 7] = 2;
    list_image[512] = 'X';
    CHECK(write(list_fd, list_image, sizeof(list_image)) == (ssize_t)sizeof(list_image));
    close(list_fd);

    char list_arg[VIEWBBC_COMMAND_ARGUMENT_MAX + 32];
    CHECK(snprintf(list_arg, sizeof(list_arg), "--mountlist:ssd:%s", list_template) > 0);
    char *argv9[] = { (char *)"viewbbc", list_arg };
    CHECK(viewbbc_startup_parse(2, argv9, &options, error, sizeof(error)));
    ViewBBCCommandOutput list_output;
    CHECK(viewbbc_startup_mountlist(&options, &list_output, error, sizeof(error)));
    CHECK(list_output.line_count == 1u);
    CHECK(strcmp(list_output.lines[0], ":0.$.HELLO") == 0);
    unlink(list_template);
}


/* Deterministic malformed-input smoke tests. These are deliberately kept in the
 * normal test binary so every release build exercises the public parsers with
 * hostile-looking input, even when libFuzzer/AFL are not installed. */
static unsigned robustness_prng(unsigned *state) {
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static void test_malformed_input_robustness(void) {
    unsigned rng = 0x42454542u;
    ViewBBCParsedCommand command;

    /* Command parser: arbitrary printable/control-ish byte strings, always NUL terminated. */
    for (size_t iteration = 0; iteration < 5000u; ++iteration) {
        char input[VIEWBBC_COMMAND_MAX + 2u];
        size_t length = robustness_prng(&rng) % (sizeof(input) - 1u);
        for (size_t i = 0; i < length; ++i) {
            unsigned v = robustness_prng(&rng);
            input[i] = (char)(1u + (v % 126u));
        }
        input[length] = '\0';
        (void)viewbbc_command_parse(input, &command);
    }

    /* DFS parser: truncated and malformed catalogues must fail cleanly. */
    char dfs_template[512];
    int dfs_fd = test_make_temp_file(dfs_template, sizeof(dfs_template), "beebview-dfs-robust");
    CHECK(dfs_fd >= 0);
    for (size_t iteration = 0; iteration < 128u; ++iteration) {
        unsigned char bytes[1024];
        size_t length = robustness_prng(&rng) % sizeof(bytes);
        for (size_t i = 0; i < length; ++i) bytes[i] = (unsigned char)robustness_prng(&rng);
        CHECK(test_truncate_fd(dfs_fd, 0) == 0);
        CHECK(lseek(dfs_fd, 0, SEEK_SET) == 0);
        if (length) CHECK(write(dfs_fd, bytes, length) == (ssize_t)length);
        CHECK(test_sync_fd(dfs_fd) == 0);
        ViewBBCDFSImage image;
        if (viewbbc_dfs_open_as(&image, dfs_template, VIEWBBC_DFS_TYPE_SSD)) viewbbc_dfs_close(&image);
        if (viewbbc_dfs_open_as(&image, dfs_template, VIEWBBC_DFS_TYPE_DSD)) viewbbc_dfs_close(&image);
    }
    close(dfs_fd);
    unlink(dfs_template);

    /* Host text loader: arbitrary byte files must either load within the workspace
     * limit or fail without damaging allocator state. */
    char file_template[512];
    int file_fd = test_make_temp_file(file_template, sizeof(file_template), "beebview-file-robust");
    CHECK(file_fd >= 0);
    for (size_t iteration = 0; iteration < 64u; ++iteration) {
        unsigned char bytes[4096];
        size_t length = robustness_prng(&rng) % sizeof(bytes);
        for (size_t i = 0; i < length; ++i) bytes[i] = (unsigned char)robustness_prng(&rng);
        CHECK(test_truncate_fd(file_fd, 0) == 0);
        CHECK(lseek(file_fd, 0, SEEK_SET) == 0);
        if (length) CHECK(write(file_fd, bytes, length) == (ssize_t)length);
        CHECK(test_sync_fd(file_fd) == 0);
        ViewBBCDocument document;
        CHECK(viewbbc_document_init(&document));
        (void)viewbbc_file_load_document(file_template, &document);
        CHECK(viewbbc_document_bytes_used(&document) <= document.workspace_limit);
        viewbbc_document_destroy(&document);
    }
    close(file_fd);
    unlink(file_template);
}

static void test_editor_resize(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, VIEWBBC_DEFAULT_COLS, VIEWBBC_DEFAULT_ROWS));
    for (int i = 0; i < 40; ++i) {
        CHECK(viewbbc_document_insert_char(&editor.document, editor.cursor_line, editor.cursor_column, 'A'));
        ++editor.cursor_column;
        CHECK(viewbbc_document_split_line(&editor.document, editor.cursor_line, editor.cursor_column));
        ++editor.cursor_line;
        editor.cursor_column = 0u;
    }
    editor.cursor_line = 35u;
    editor.cursor_column = 0u;
    editor.viewport_top = 20u;
    viewbbc_editor_render(&editor);
    CHECK(viewbbc_editor_resize(&editor, 120, 40));
    CHECK(editor.screen.cols == 120);
    CHECK(editor.screen.rows == 40);
    CHECK(editor.cursor_line >= editor.viewport_top);
    CHECK(editor.cursor_line < editor.viewport_top + (size_t)(editor.screen.rows - 1));
    CHECK(!viewbbc_editor_resize(&editor, 2, 2));
    CHECK(editor.screen.cols == 120 && editor.screen.rows == 40);
    viewbbc_editor_destroy(&editor);
}



static void test_command_selection_editing(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 10));
    const char *command = "LOAD file";
    for (size_t i = 0; command[i] != '\0'; ++i)
        viewbbc_editor_insert_char(&editor, (uint8_t)command[i]);
    CHECK(strcmp(editor.command_buffer, command) == 0);

    int prompt_y = editor.screen.cursor_y;
    /* Mouse drag over the final four characters, just as a desktop command
       line edit control would. */
    viewbbc_editor_command_mouse_down(&editor, 2 + 5, prompt_y);
    viewbbc_editor_command_mouse_drag(&editor, 2 + 9, prompt_y);
    viewbbc_editor_command_mouse_up(&editor, 2 + 9, prompt_y);
    CHECK(viewbbc_editor_command_has_selection(&editor));
    CHECK(editor.command_selection_start == 5u);
    CHECK(editor.command_selection_end == 9u);
    CHECK(viewbbc_screen_get(&editor.screen, 2 + 5, prompt_y).attr & VIEWBBC_ATTR_REVERSE);

    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_BACKSPACE);
    CHECK(strcmp(editor.command_buffer, "LOAD ") == 0);
    CHECK(!viewbbc_editor_command_has_selection(&editor));

    /* Keyboard selection follows normal desktop Shift+cursor semantics. */
    editor.command_cursor = editor.command_length;
    viewbbc_editor_command_clear_selection(&editor);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_LEFT);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_LEFT);
    CHECK(viewbbc_editor_command_has_selection(&editor));
    CHECK(editor.command_selection_end - editor.command_selection_start == 2u);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_RIGHT);
    CHECK(editor.command_selection_end - editor.command_selection_start == 1u);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_RIGHT);
    CHECK(!viewbbc_editor_command_has_selection(&editor));

    viewbbc_editor_command_select_all(&editor);
    CHECK(editor.command_selection_start == 0u);
    CHECK(editor.command_selection_end == editor.command_length);
    viewbbc_editor_insert_char(&editor, 'C');
    CHECK(strcmp(editor.command_buffer, "C") == 0);
    CHECK(!viewbbc_editor_command_has_selection(&editor));
    viewbbc_editor_destroy(&editor);
}

static void test_edit_delete_command_keys(void) {
    ViewBBCEditor editor;
    CHECK(viewbbc_editor_init(&editor, 80, 25));
    editor.mode = VIEWBBC_MODE_TEXT;
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F8);
    CHECK(editor.edit_command_active);
    viewbbc_editor_insert_char(&editor, 'c');
    viewbbc_editor_insert_char(&editor, 'e');
    CHECK(editor.edit_command_length == 2u);
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_RETURN);
    CHECK(!editor.edit_command_active);
    CHECK(strcmp(viewbbc_document_command_at(&editor.document, 0), "CE") == 0);
    viewbbc_editor_insert_char(&editor, 'T');
    CHECK(viewbbc_document_char_at(&editor.document, 0, 0) == 'T');
    viewbbc_editor_handle_key(&editor, VIEWBBC_KEY_SHIFT_F9);
    CHECK(viewbbc_document_command_at(&editor.document, 0)[0] == '\0');
    CHECK(viewbbc_document_char_at(&editor.document, 0, 0) == 'T');
    viewbbc_editor_destroy(&editor);

    ViewBBCDocument doc;
    CHECK(viewbbc_document_init(&doc));
    const uint8_t stored[] = {'P','L','\t','4','5','\r','T','e','x','t'};
    CHECK(viewbbc_document_load_bytes(&doc, stored, sizeof(stored)));
    CHECK(strcmp(viewbbc_document_command_at(&doc, 0), "PL") == 0);
    CHECK(viewbbc_document_line_length(&doc, 0) == 2u);
    CHECK(viewbbc_document_char_at(&doc, 0, 0) == '4');
    viewbbc_document_destroy(&doc);
}

int main(void) {
    test_screen();
    test_editor_resize();
    test_command_selection_editing();
    test_edit_delete_command_keys();
    test_document_editing();
    test_workspace_limit();
    test_load_bytes();
    test_document_command_operations();
    test_command_parser();
    test_dfs_read_only();
    test_native_path_separators();
    test_native_directory_listing();
    test_filesystem_commands();
    test_command_console_history();
    test_command_paging();
    test_view_key_semantics();
    test_markers();
    test_block_operations();
    test_format_block();
    test_modified_load_and_exit_commands();
    test_inline_exit_confirmation();
    test_view_command_batch();
    test_marker_aware_search_and_count();
    test_change_replace_fold();
    test_global_format_and_immediate_editing();
    test_mouse_operations();
    test_print_export();
    test_setup_command();
    test_buffersize_command_and_config();
    test_version_and_line_numbers();
    test_command_line_editing_and_completion();
    test_startup_arguments();
    test_config_defaults_and_parser();
    test_malformed_input_robustness();
    puts("ViewBBC core tests passed");
    return 0;
}
