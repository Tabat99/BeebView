#ifndef VIEWBBC_CONFIG_SCREEN_H
#define VIEWBBC_CONFIG_SCREEN_H

#include <SDL3/SDL.h>
#include "viewbbc/config.h"

int viewbbc_config_screen_run(SDL_Window *window, SDL_Renderer *renderer,
                              ViewBBCConfig *config, float font_scale,
                              int logical_width, int logical_height);

#endif
