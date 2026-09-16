#include "sdl3_frontend.h"

#include <SDL3/SDL.h>

int viewbbc_sdl3_set_clipboard_text(const char *text) {
    return text && SDL_SetClipboardText(text);
}

char *viewbbc_sdl3_get_clipboard_text(void) {
    return SDL_GetClipboardText();
}

int viewbbc_sdl3_set_primary_selection_text(const char *text) {
    return text && SDL_SetPrimarySelectionText(text);
}

char *viewbbc_sdl3_get_primary_selection_text(void) {
    return SDL_GetPrimarySelectionText();
}

void viewbbc_sdl3_free_text(char *text) {
    SDL_free(text);
}

