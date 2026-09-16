#ifndef VIEWBBC_SDL3_FRONTEND_H
#define VIEWBBC_SDL3_FRONTEND_H

#include <stdint.h>
#include <stddef.h>
#include "viewbbc/editor.h"
#include "viewbbc/config.h"

typedef enum {
    VIEWBBC_SDL_MOUSE_NONE = 0,
    VIEWBBC_SDL_MOUSE_DOWN,
    VIEWBBC_SDL_MOUSE_DRAG,
    VIEWBBC_SDL_MOUSE_UP,
    VIEWBBC_SDL_MOUSE_WHEEL
} ViewBBCSDLMouseType;

typedef enum {
    VIEWBBC_SDL_CLIPBOARD_NONE = 0,
    VIEWBBC_SDL_CLIPBOARD_COPY,
    VIEWBBC_SDL_CLIPBOARD_PASTE,
    VIEWBBC_SDL_CLIPBOARD_CUT,
    VIEWBBC_SDL_CLIPBOARD_SELECT_ALL
} ViewBBCSDLClipboardAction;

typedef struct {
    ViewBBCSDLMouseType type;
    int cell_x;
    int cell_y;
    int wheel_steps;
    int button;
    int clicks;
    unsigned modifiers;
} ViewBBCSDLMouseEvent;

int viewbbc_sdl3_init(const ViewBBCConfig *config);
void viewbbc_sdl3_shutdown(void);
void viewbbc_sdl3_render(const ViewBBCScreen *screen);
void viewbbc_sdl3_get_grid_size(int *cols, int *rows);
void viewbbc_sdl3_apply_config(const ViewBBCConfig *config);
int viewbbc_sdl3_run_config(ViewBBCConfig *config);
int viewbbc_sdl3_wait_input(ViewBBCKey *key, uint32_t *ch, int *is_text,
                            ViewBBCSDLMouseEvent *mouse,
                            ViewBBCSDLClipboardAction *clipboard_action);
int viewbbc_sdl3_set_clipboard_text(const char *text);
char *viewbbc_sdl3_get_clipboard_text(void);
int viewbbc_sdl3_set_primary_selection_text(const char *text);
char *viewbbc_sdl3_get_primary_selection_text(void);
void viewbbc_sdl3_free_text(char *text);
int viewbbc_sdl3_choose_print_file(char *target, size_t target_size);

#endif
