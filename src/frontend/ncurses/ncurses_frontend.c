#include "ncurses_frontend.h"

#include <curses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif

#include "viewbbc/config.h"

#define VIEWBBC_ESCAPE_DELAY_MS 50
#ifdef NCURSES_VERSION
#define VIEWBBC_NCURSES_PASTE_START (KEY_MAX + 1)
#endif

static const ViewBBCConfig *g_config = NULL;
static unsigned char *g_paste = NULL;
static size_t g_paste_len = 0;
static size_t g_paste_pos = 0;

static void paste_clear(void) {
    free(g_paste);
    g_paste = NULL;
    g_paste_len = 0;
    g_paste_pos = 0;
}

static int paste_append(unsigned char ch) {
    unsigned char *grown = realloc(g_paste, g_paste_len + 1u);
    if (!grown) return 0;
    g_paste = grown;
    g_paste[g_paste_len++] = ch;
    return 1;
}

#ifdef NCURSES_VERSION
static int collect_bracketed_paste(void) {
    static const unsigned char end_seq[] = { 27u, '[', '2', '0', '1', '~' };
    size_t matched = 0;
    int ok = 1;

    paste_clear();
    keypad(stdscr, FALSE);
    for (;;) {
        int c = getch();
        if (c == ERR) { ok = 0; break; }
        unsigned char ch = (unsigned char)(c & 0xff);

        if (ch == end_seq[matched]) {
            if (++matched == sizeof(end_seq)) break;
            continue;
        }

        if (matched != 0u) {
            for (size_t i = 0; i < matched; ++i) {
                if (!paste_append(end_seq[i])) { ok = 0; break; }
            }
            matched = 0;
            if (!ok) break;
            if (ch == end_seq[0]) { matched = 1; continue; }
        }
        if (!paste_append(ch)) { ok = 0; break; }
    }
    keypad(stdscr, TRUE);

    if (!ok) { paste_clear(); return 0; }

    /* Normalize terminal line endings and discard control bytes that should
       never become VIEW commands during a paste. */
    size_t out = 0;
    for (size_t i = 0; i < g_paste_len; ++i) {
        unsigned char ch = g_paste[i];
        if (ch == '\r') {
            if (i + 1u < g_paste_len && g_paste[i + 1u] == '\n') ++i;
            g_paste[out++] = '\n';
        } else if (ch == '\n' || ch == '\t' || (ch >= 32u && ch <= 126u)) {
            g_paste[out++] = ch;
        }
    }
    g_paste_len = out;
    g_paste_pos = 0;
    return 1;
}
#endif

static int emit_paste_event(ViewBBCKey *key, uint32_t *ch, int *is_text) {
    if (g_paste_pos >= g_paste_len) {
        paste_clear();
        return 0;
    }
    unsigned char c = g_paste[g_paste_pos++];
    if (c == '\n') *key = VIEWBBC_KEY_RETURN;
    else if (c == '\t') *key = VIEWBBC_KEY_TAB;
    else { *ch = (uint32_t)c; *is_text = 1; }
    if (g_paste_pos >= g_paste_len) {
        free(g_paste);
        g_paste = NULL;
        g_paste_len = g_paste_pos = 0;
    }
    return 1;
}

int viewbbc_ncurses_init(const ViewBBCConfig *config) {
    g_config = config;
    if (initscr() == NULL) return 0;
#ifdef NCURSES_VERSION
    /* ncurses otherwise waits about a second to decide whether ESC starts
       a function-key escape sequence.  VIEW uses ESC to switch modes, so
       keep that ambiguity window short enough to feel immediate. */
    set_escdelay(VIEWBBC_ESCAPE_DELAY_MS);
#endif
    raw();
    noecho();
    keypad(stdscr, TRUE);
#ifdef NCURSES_VERSION
    /* Ask xterm-compatible terminals to wrap paste operations in markers so
       pasted escape/control bytes cannot be mistaken for VIEW commands. */
    define_key("\033[200~", VIEWBBC_NCURSES_PASTE_START);
    fputs("\033[?2004h", stdout);
    fflush(stdout);
#endif
    curs_set(1);
    return 1;
}

void viewbbc_ncurses_shutdown(void) {
#ifdef NCURSES_VERSION
    fputs("\033[?2004l", stdout);
    fflush(stdout);
#endif
    paste_clear();
    endwin();
    g_config = NULL;
}

void viewbbc_ncurses_render(const ViewBBCScreen *screen) {
    erase();

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    int rows = screen->rows < max_y ? screen->rows : max_y;
    int cols = screen->cols < max_x ? screen->cols : max_x;

    for (int y = 0; y < rows; ++y) {
        move(y, 0);
        for (int x = 0; x < cols; ++x) {
            ViewBBCCell cell = viewbbc_screen_get(screen, x, y);
            if (cell.attr & VIEWBBC_ATTR_REVERSE) attron(A_REVERSE);
            if (cell.attr & VIEWBBC_ATTR_BOLD) attron(A_BOLD);
            addch((chtype)(cell.ch <= 0x7f ? cell.ch : '?'));
            if (cell.attr & VIEWBBC_ATTR_BOLD) attroff(A_BOLD);
            if (cell.attr & VIEWBBC_ATTR_REVERSE) attroff(A_REVERSE);
        }
    }

    if (screen->cursor_visible && screen->cursor_y >= 0 && screen->cursor_x >= 0 &&
        screen->cursor_y < max_y && screen->cursor_x < max_x) {
        (void)curs_set(1);
        move(screen->cursor_y, screen->cursor_x);
    } else {
        (void)curs_set(0);
    }
    refresh();
}

static ViewBBCPhysicalKey physical_from_ncurses(int c) {
    ViewBBCPhysicalKey p = { VIEWBBC_PHYS_NONE, 0 };
    switch (c) {
        case KEY_LEFT:p.base=VIEWBBC_PHYS_LEFT;return p; case KEY_RIGHT:p.base=VIEWBBC_PHYS_RIGHT;return p;
        case KEY_UP:p.base=VIEWBBC_PHYS_UP;return p; case KEY_DOWN:p.base=VIEWBBC_PHYS_DOWN;return p;
        case KEY_HOME:p.base=VIEWBBC_PHYS_HOME;return p; case KEY_END:p.base=VIEWBBC_PHYS_END;return p;
        case KEY_PPAGE:p.base=VIEWBBC_PHYS_PAGEUP;return p; case KEY_NPAGE:p.base=VIEWBBC_PHYS_PAGEDOWN;return p;
#ifdef KEY_IC
        case KEY_IC:p.base=VIEWBBC_PHYS_INSERT;return p;
#endif
        case KEY_BACKSPACE: case 127:p.base=VIEWBBC_PHYS_BACKSPACE;return p;
        case KEY_DC:p.base=VIEWBBC_PHYS_DELETE;return p; case '\t':p.base=VIEWBBC_PHYS_TAB;return p;
        case '\n': case '\r':p.base=VIEWBBC_PHYS_ENTER;return p; case 27:p.base=VIEWBBC_PHYS_ESCAPE;return p;
        default: break;
    }
    if (c >= 1 && c <= 26) { p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_A+c-1); p.modifiers=VIEWBBC_MOD_CTRL; return p; }
    for (int n=1;n<=12;++n) if (c==KEY_F(n)) {p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_F1+n-1);return p;}
    for (int n=1;n<=10;++n) if (c==KEY_F(n+12)) {p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_F1+n-1);p.modifiers=VIEWBBC_MOD_SHIFT;return p;}
    for (int n=1;n<=10;++n) if (c==KEY_F(n+24)) {p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_F1+n-1);p.modifiers=VIEWBBC_MOD_CTRL;return p;}
    if (c>='A'&&c<='Z') {p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_A+c-'A');p.modifiers=VIEWBBC_MOD_SHIFT;return p;}
    if (c>='a'&&c<='z') {p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_A+c-'a');return p;}
    return p;
}
int viewbbc_ncurses_read_input(ViewBBCKey *key, uint32_t *ch, int *is_text) {
    *key = VIEWBBC_KEY_NONE; *ch = 0; *is_text = 0;
    if (emit_paste_event(key, ch, is_text)) return 1;

    int c = getch();
#ifdef NCURSES_VERSION
    if (c == VIEWBBC_NCURSES_PASTE_START) {
        if (!collect_bracketed_paste()) return 0;
        return emit_paste_event(key, ch, is_text);
    }
#endif
    if (c == KEY_F(12)) { *key = VIEWBBC_KEY_BREAK; return 1; }
    ViewBBCPhysicalKey physical = physical_from_ncurses(c);
    ViewBBCKey mapped = viewbbc_config_key_for_physical(g_config, physical);
    if (mapped != VIEWBBC_KEY_NONE) { *key = mapped; return 1; }
    if (c >= 32 && c <= 126) { *ch=(uint32_t)c; *is_text=1; return 1; }
    return 0;
}

int viewbbc_ncurses_choose_print_file(char *target, size_t target_size) {
    if (!target || target_size < 8u) return 0;
    char type[16]={0}, path[VIEWBBC_CONFIG_VALUE_MAX+1]={0};
    echo(); curs_set(1); erase();
    mvaddstr(0,0,"PRINT TO FILE");
    mvaddstr(2,0,"Type (text/pdf/odt): "); refresh();
    if (getnstr(type,(int)sizeof(type)-1)==ERR) { noecho(); return 0; }
    const char *prefix=NULL,*ext=NULL;
    if (strcasecmp(type,"text")==0 || strcasecmp(type,"txt")==0) { prefix="text:"; ext=".txt"; }
    else if (strcasecmp(type,"pdf")==0) { prefix="pdf:"; ext=".pdf"; }
    else if (strcasecmp(type,"odt")==0 || strcasecmp(type,"odf")==0) { prefix="odt:"; ext=".odt"; }
    else { noecho(); return 0; }
    mvaddstr(4,0,"Filename: "); refresh();
    if (getnstr(path,(int)sizeof(path)-1)==ERR || !path[0]) { noecho(); return 0; }
    size_t len=strlen(path),elen=strlen(ext); if(len<elen || strcmp(path+len-elen,ext)!=0){if(len+elen>=sizeof(path)){noecho();return 0;}strcat(path,ext);}
    FILE *f=fopen(path,"rb"); if(f){fclose(f); char answer[8]={0}; mvprintw(6,0,"%s exists. Overwrite? (y/N): ",path); refresh(); if(getnstr(answer,7)==ERR || !(answer[0]=='y'||answer[0]=='Y')){noecho();return 0;}}
    noecho();
    if(strlen(prefix)+strlen(path)+1u>target_size)return 0;
    snprintf(target,target_size,"%s%s",prefix,path); return 1;
}
