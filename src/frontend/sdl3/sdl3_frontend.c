#include "sdl3_frontend.h"
#include "bitmap_font.h"
#include "config_screen.h"
#include "viewbbc/version.h"
#include "viewbbc/config.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

static float g_font_scale = 1.0f;
static float g_cell_w = (float)VIEWBBC_BITMAP_FONT_WIDTH;
static float g_cell_h = (float)VIEWBBC_BITMAP_FONT_HEIGHT;
static int g_grid_cols = VIEWBBC_DEFAULT_COLS;
static int g_grid_rows = VIEWBBC_DEFAULT_ROWS;
static int g_logical_w = VIEWBBC_DEFAULT_COLS * VIEWBBC_BITMAP_FONT_WIDTH;
static int g_logical_h = VIEWBBC_DEFAULT_ROWS * VIEWBBC_BITMAP_FONT_HEIGHT;

#define VIEWBBC_MAX_DYNAMIC_COLS 512
#define VIEWBBC_MAX_DYNAMIC_ROWS 256
#define VIEWBBC_CELL_W (g_cell_w)
#define VIEWBBC_CELL_H (g_cell_h)
#define VIEWBBC_LOGICAL_W (g_logical_w)
#define VIEWBBC_LOGICAL_H (g_logical_h)

static SDL_Window *g_window = NULL;
static const ViewBBCConfig *g_config = NULL;
static SDL_Renderer *g_renderer = NULL;
static Uint64 g_cursor_blink_epoch = 0;
static int g_cursor_visible = 1;
static float g_display_content_scale = 1.0f;

#define VIEWBBC_CURSOR_BLINK_MS 500u
#define VIEWBBC_INPUT_POLL_MS 50


static float display_content_scale(SDL_DisplayID display) {
    float scale = 1.0f;

    if (display != 0) {
        const float reported = SDL_GetDisplayContentScale(display);
        if (reported > 0.0f) scale = reported;
    }

    /* Avoid pathological platform values from creating an unusable window. */
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 4.0f) scale = 4.0f;
    return scale;
}

static int scaled_window_dimension(int logical, float scale) {
    const float scaled = (float)logical * scale;
    if (scaled >= 32767.0f) return 32767;
    if (scaled <= 1.0f) return 1;
    return (int)(scaled + 0.5f);
}

static int clamp_grid_dimension(int value, int minimum, int maximum) {
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static void apply_grid_presentation(void) {
    g_logical_w = (int)((float)g_grid_cols * g_cell_w + 0.5f);
    g_logical_h = (int)((float)g_grid_rows * g_cell_h + 0.5f);
    if (g_renderer) {
        SDL_SetRenderLogicalPresentation(g_renderer, VIEWBBC_LOGICAL_W, VIEWBBC_LOGICAL_H,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
    }
}

static void update_grid_from_window(void) {
    if (!g_window) return;
    int width = 0, height = 0;
    if (!SDL_GetWindowSize(g_window, &width, &height)) return;

    float scale = g_display_content_scale > 0.0f ? g_display_content_scale : 1.0f;
    float cell_width = g_cell_w * scale;
    float cell_height = g_cell_h * scale;
    if (cell_width <= 0.0f || cell_height <= 0.0f) return;

    int cols = (int)((float)width / cell_width);
    int rows = (int)((float)height / cell_height);
    g_grid_cols = clamp_grid_dimension(cols, VIEWBBC_DEFAULT_COLS, VIEWBBC_MAX_DYNAMIC_COLS);
    g_grid_rows = clamp_grid_dimension(rows, VIEWBBC_DEFAULT_ROWS, VIEWBBC_MAX_DYNAMIC_ROWS);
    apply_grid_presentation();
}

static void set_minimum_window_size(void) {
    if (!g_window) return;
    int min_w = scaled_window_dimension((int)((float)VIEWBBC_DEFAULT_COLS * g_cell_w + 0.5f),
                                        g_display_content_scale);
    int min_h = scaled_window_dimension((int)((float)VIEWBBC_DEFAULT_ROWS * g_cell_h + 0.5f),
                                        g_display_content_scale);
    SDL_SetWindowMinimumSize(g_window, min_w, min_h);
}

static void rescale_window_for_display(void) {
    if (!g_window) return;

    SDL_DisplayID display = SDL_GetDisplayForWindow(g_window);
    const float new_scale = display_content_scale(display);
    const float old_scale = g_display_content_scale;

    if (old_scale <= 0.0f) {
        g_display_content_scale = new_scale;
        return;
    }

    /* Ignore tiny floating-point/reporting changes. */
    float delta = new_scale - old_scale;
    if (delta < 0.0f) delta = -delta;
    if (delta < 0.01f) return;

    int width = 0;
    int height = 0;
    if (SDL_GetWindowSize(g_window, &width, &height)) {
        const float ratio = new_scale / old_scale;
        const int new_width = scaled_window_dimension(width, ratio);
        const int new_height = scaled_window_dimension(height, ratio);
        SDL_SetWindowSize(g_window, new_width, new_height);
    }

    g_display_content_scale = new_scale;
    set_minimum_window_size();
    update_grid_from_window();
}

static ViewBBCPhysicalKey physical_from_sdl(SDL_Keycode sym, SDL_Keymod mod) {
    ViewBBCPhysicalKey p = { VIEWBBC_PHYS_NONE, 0 };
    if (mod & SDL_KMOD_CTRL) p.modifiers |= VIEWBBC_MOD_CTRL;
    if (mod & SDL_KMOD_SHIFT) p.modifiers |= VIEWBBC_MOD_SHIFT;
    if (mod & SDL_KMOD_ALT) p.modifiers |= VIEWBBC_MOD_ALT;
    switch (sym) {
        case SDLK_LEFT: p.base=VIEWBBC_PHYS_LEFT; break; case SDLK_RIGHT:p.base=VIEWBBC_PHYS_RIGHT;break;
        case SDLK_UP:p.base=VIEWBBC_PHYS_UP;break; case SDLK_DOWN:p.base=VIEWBBC_PHYS_DOWN;break;
        case SDLK_HOME:p.base=VIEWBBC_PHYS_HOME;break; case SDLK_END:p.base=VIEWBBC_PHYS_END;break;
        case SDLK_PAGEUP:p.base=VIEWBBC_PHYS_PAGEUP;break; case SDLK_PAGEDOWN:p.base=VIEWBBC_PHYS_PAGEDOWN;break;
        case SDLK_INSERT:p.base=VIEWBBC_PHYS_INSERT;break; case SDLK_TAB:p.base=VIEWBBC_PHYS_TAB;break;
        case SDLK_BACKSPACE:p.base=VIEWBBC_PHYS_BACKSPACE;break; case SDLK_DELETE:p.base=VIEWBBC_PHYS_DELETE;break;
        case SDLK_RETURN: case SDLK_KP_ENTER:p.base=VIEWBBC_PHYS_ENTER;break; case SDLK_ESCAPE:p.base=VIEWBBC_PHYS_ESCAPE;break;
        case SDLK_F1:p.base=VIEWBBC_PHYS_F1;break;case SDLK_F2:p.base=VIEWBBC_PHYS_F2;break;case SDLK_F3:p.base=VIEWBBC_PHYS_F3;break;
        case SDLK_F4:p.base=VIEWBBC_PHYS_F4;break;case SDLK_F5:p.base=VIEWBBC_PHYS_F5;break;case SDLK_F6:p.base=VIEWBBC_PHYS_F6;break;
        case SDLK_F7:p.base=VIEWBBC_PHYS_F7;break;case SDLK_F8:p.base=VIEWBBC_PHYS_F8;break;case SDLK_F9:p.base=VIEWBBC_PHYS_F9;break;
        case SDLK_F10:p.base=VIEWBBC_PHYS_F10;break;case SDLK_F11:p.base=VIEWBBC_PHYS_F11;break;case SDLK_F12:p.base=VIEWBBC_PHYS_F12;break;
        case SDLK_SPACE:p.base=VIEWBBC_PHYS_SPACE;break;case SDLK_MINUS:p.base=VIEWBBC_PHYS_MINUS;break;case SDLK_EQUALS:p.base=VIEWBBC_PHYS_EQUALS;break;
        case SDLK_LEFTBRACKET:p.base=VIEWBBC_PHYS_LEFTBRACKET;break;case SDLK_RIGHTBRACKET:p.base=VIEWBBC_PHYS_RIGHTBRACKET;break;case SDLK_BACKSLASH:p.base=VIEWBBC_PHYS_BACKSLASH;break;
        case SDLK_SEMICOLON:p.base=VIEWBBC_PHYS_SEMICOLON;break;case SDLK_APOSTROPHE:p.base=VIEWBBC_PHYS_APOSTROPHE;break;case SDLK_GRAVE:p.base=VIEWBBC_PHYS_GRAVE;break;
        case SDLK_COMMA:p.base=VIEWBBC_PHYS_COMMA;break;case SDLK_PERIOD:p.base=VIEWBBC_PHYS_PERIOD;break;case SDLK_SLASH:p.base=VIEWBBC_PHYS_SLASH;break;
        default:
            if (sym >= SDLK_0 && sym <= SDLK_9) p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_0 + (sym-SDLK_0));
            else if (sym >= SDLK_A && sym <= SDLK_Z) p.base=(ViewBBCPhysicalBase)(VIEWBBC_PHYS_A + (sym-SDLK_A));
            break;
    }
    return p;
}
void viewbbc_sdl3_apply_config(const ViewBBCConfig *config) {
    g_config = config;
    if (!config) return;
    g_font_scale = (float)config->font_size / (float)VIEWBBC_BITMAP_FONT_HEIGHT;
    g_cell_w = (float)VIEWBBC_BITMAP_FONT_WIDTH * g_font_scale;
    g_cell_h = (float)VIEWBBC_BITMAP_FONT_HEIGHT * g_font_scale;
    if (g_window) {
        set_minimum_window_size();
        update_grid_from_window();
    } else {
        g_grid_cols = VIEWBBC_DEFAULT_COLS;
        g_grid_rows = VIEWBBC_DEFAULT_ROWS;
        apply_grid_presentation();
    }
}

int viewbbc_sdl3_run_config(ViewBBCConfig *config) {
    int accepted = viewbbc_config_screen_run(g_window, g_renderer, config, g_font_scale, VIEWBBC_LOGICAL_W, VIEWBBC_LOGICAL_H);
    if (accepted) viewbbc_sdl3_apply_config(config);
    g_cursor_visible = 1; g_cursor_blink_epoch = SDL_GetTicks();
    return accepted;
}

int viewbbc_sdl3_init(const ViewBBCConfig *config) {
    viewbbc_sdl3_apply_config(config);
    if (config && strcmp(config->font, "mode7") != 0 && strcmp(config->font, "bedstead") != 0)
        SDL_Log("Unsupported configured font '%s'; using mode7", config->font);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 0;

    SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    g_display_content_scale = display_content_scale(primary);
    const int window_width = scaled_window_dimension(VIEWBBC_LOGICAL_W,
                                                      g_display_content_scale);
    const int window_height = scaled_window_dimension(VIEWBBC_LOGICAL_H,
                                                       g_display_content_scale);

    if (!SDL_CreateWindowAndRenderer(VIEWBBC_PRODUCT_NAME,
                                     window_width,
                                     window_height,
                                     SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
                                     &g_window,
                                     &g_renderer)) {
        SDL_Quit();
        return 0;
    }

    set_minimum_window_size();
    update_grid_from_window();
    SDL_StartTextInput(g_window);
    g_cursor_blink_epoch = SDL_GetTicks();
    g_cursor_visible = 1;
    return 1;
}

void viewbbc_sdl3_shutdown(void) {
    if (g_window) SDL_StopTextInput(g_window);
    if (g_renderer) SDL_DestroyRenderer(g_renderer);
    if (g_window) SDL_DestroyWindow(g_window);
    g_renderer = NULL;
    g_window = NULL;
    g_config = NULL;
    SDL_Quit();
}

static void draw_cell(int x, int y, ViewBBCCell cell, int cursor) {
    const int reverse = (cell.attr & VIEWBBC_ATTR_REVERSE) != 0;
    const Uint8 fg = reverse ? 0 : 255;
    const Uint8 bg = reverse ? 255 : 0;
    SDL_FRect rect = {
        (float)x * VIEWBBC_CELL_W,
        (float)y * VIEWBBC_CELL_H,
        VIEWBBC_CELL_W,
        VIEWBBC_CELL_H
    };

    SDL_SetRenderDrawColor(g_renderer, bg, bg, bg, 255);
    SDL_RenderFillRect(g_renderer, &rect);

    if (cell.ch >= 32 && cell.ch <= 126) {
        const uint16_t *glyph = viewbbc_bitmap_font_glyph(cell.ch);
        SDL_FRect pixels[VIEWBBC_BITMAP_FONT_WIDTH * VIEWBBC_BITMAP_FONT_HEIGHT];
        int pixel_count = 0;

        for (int gy = 0; gy < VIEWBBC_BITMAP_FONT_HEIGHT; ++gy) {
            uint16_t bits = glyph[gy];
            for (int gx = 0; gx < VIEWBBC_BITMAP_FONT_WIDTH; ++gx) {
                if ((bits & (uint16_t)(0x8000u >> gx)) == 0) continue;
                pixels[pixel_count++] = (SDL_FRect){
                    rect.x + (float)gx,
                    rect.y + (float)gy,
                    g_font_scale,
                    g_font_scale
                };
            }
        }

        if (pixel_count > 0) {
            SDL_SetRenderDrawColor(g_renderer, fg, fg, fg, 255);
            SDL_RenderFillRects(g_renderer, pixels, pixel_count);
        }
    }

    /* Authentic BBC VIEW cursor: a thick bar across the bottom of the cell. */
    if (cursor) {
        SDL_FRect cursor_rect = {
            rect.x,
            rect.y + VIEWBBC_CELL_H - (2.0f * g_font_scale),
            VIEWBBC_CELL_W,
            2.0f * g_font_scale
        };
        SDL_SetRenderDrawColor(g_renderer, fg, fg, fg, 255);
        SDL_RenderFillRect(g_renderer, &cursor_rect);
    }
}

void viewbbc_sdl3_render(const ViewBBCScreen *screen) {
    if (!g_renderer || !screen) return;

    Uint64 now = SDL_GetTicks();
    if (now - g_cursor_blink_epoch >= VIEWBBC_CURSOR_BLINK_MS) {
        Uint64 periods = (now - g_cursor_blink_epoch) / VIEWBBC_CURSOR_BLINK_MS;
        if (periods & 1u) g_cursor_visible = !g_cursor_visible;
        g_cursor_blink_epoch += periods * VIEWBBC_CURSOR_BLINK_MS;
    }

    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);

    for (int y = 0; y < screen->rows; ++y) {
        for (int x = 0; x < screen->cols; ++x) {
            ViewBBCCell cell = viewbbc_screen_get(screen, x, y);
            int cursor = screen->cursor_visible && g_cursor_visible &&
                         (x == screen->cursor_x && y == screen->cursor_y);
            draw_cell(x, y, cell, cursor);
        }
    }

    SDL_RenderPresent(g_renderer);
}

static int mouse_cell_from_window(float window_x, float window_y, int *cell_x, int *cell_y) {
    if (!g_renderer || !cell_x || !cell_y) return 0;
    float logical_x = 0.0f, logical_y = 0.0f;
    if (!SDL_RenderCoordinatesFromWindow(g_renderer, window_x, window_y,
                                         &logical_x, &logical_y)) return 0;
    if (logical_x < 0.0f || logical_y < 0.0f ||
        logical_x >= (float)(g_grid_cols * VIEWBBC_CELL_W) ||
        logical_y >= (float)(g_grid_rows * VIEWBBC_CELL_H))
        return 0;
    *cell_x = (int)(logical_x / VIEWBBC_CELL_W);
    *cell_y = (int)(logical_y / VIEWBBC_CELL_H);
    return 1;
}

static int mouse_cell_from_window_drag(float window_x, float window_y, int *cell_x, int *cell_y) {
    if (!g_renderer || !cell_x || !cell_y) return 0;
    float logical_x = 0.0f, logical_y = 0.0f;
    if (!SDL_RenderCoordinatesFromWindow(g_renderer, window_x, window_y,
                                         &logical_x, &logical_y)) return 0;
    if (logical_x < 0.0f) *cell_x = -1;
    else if (logical_x >= (float)(g_grid_cols * VIEWBBC_CELL_W)) *cell_x = g_grid_cols;
    else *cell_x = (int)(logical_x / VIEWBBC_CELL_W);
    if (logical_y < 0.0f) *cell_y = -1;
    else if (logical_y >= (float)(g_grid_rows * VIEWBBC_CELL_H)) *cell_y = g_grid_rows;
    else *cell_y = (int)(logical_y / VIEWBBC_CELL_H);
    return 1;
}

static void draw_context_menu(int x, int y) {
    static const char *items[] = { "Cut", "Copy", "Paste", "Select All" };
    const int width = 12;
    const int count = 4;
    for (int row = 0; row < count; ++row) {
        for (int col = 0; col < width; ++col) {
            ViewBBCCell cell = { ' ', VIEWBBC_ATTR_REVERSE };
            draw_cell(x + col, y + row, cell, 0);
        }
        for (int col = 0; items[row][col] && col < width - 2; ++col) {
            ViewBBCCell cell = { (uint8_t)items[row][col], VIEWBBC_ATTR_REVERSE };
            draw_cell(x + 1 + col, y + row, cell, 0);
        }
    }
    SDL_RenderPresent(g_renderer);
}

static ViewBBCSDLClipboardAction run_context_menu(int cell_x, int cell_y) {
    const int width = 12;
    const int count = 4;
    int x = cell_x;
    int y = cell_y;
    if (x + width > g_grid_cols) x = g_grid_cols - width;
    if (y + count > g_grid_rows) y = g_grid_rows - count;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    draw_context_menu(x, y);

    SDL_Event event;
    for (;;) {
        if (!SDL_WaitEvent(&event)) return VIEWBBC_SDL_CLIPBOARD_NONE;
        if (event.type == SDL_EVENT_QUIT) {
            SDL_PushEvent(&event);
            return VIEWBBC_SDL_CLIPBOARD_NONE;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_F12))
            return VIEWBBC_SDL_CLIPBOARD_NONE;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT) {
            int cx = 0, cy = 0;
            if (!mouse_cell_from_window(event.button.x, event.button.y, &cx, &cy))
                return VIEWBBC_SDL_CLIPBOARD_NONE;
            if (cx < x || cx >= x + width || cy < y || cy >= y + count)
                return VIEWBBC_SDL_CLIPBOARD_NONE;
            switch (cy - y) {
                case 0: return VIEWBBC_SDL_CLIPBOARD_CUT;
                case 1: return VIEWBBC_SDL_CLIPBOARD_COPY;
                case 2: return VIEWBBC_SDL_CLIPBOARD_PASTE;
                case 3: return VIEWBBC_SDL_CLIPBOARD_SELECT_ALL;
                default: return VIEWBBC_SDL_CLIPBOARD_NONE;
            }
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
            return VIEWBBC_SDL_CLIPBOARD_NONE;
    }
}

void viewbbc_sdl3_get_grid_size(int *cols, int *rows) {
    if (cols) *cols = g_grid_cols;
    if (rows) *rows = g_grid_rows;
}

int viewbbc_sdl3_wait_input(ViewBBCKey *key, uint32_t *ch, int *is_text,
                            ViewBBCSDLMouseEvent *mouse,
                            ViewBBCSDLClipboardAction *clipboard_action) {
    SDL_Event event;
    *key = VIEWBBC_KEY_NONE;
    *ch = 0;
    *is_text = 0;
    if (mouse) *mouse = (ViewBBCSDLMouseEvent){0};
    if (clipboard_action) *clipboard_action = VIEWBBC_SDL_CLIPBOARD_NONE;

    for (;;) {
        if (!SDL_WaitEventTimeout(&event, VIEWBBC_INPUT_POLL_MS)) {
            /* No input: return a no-op so the caller can repaint the blinking cursor. */
            *key = VIEWBBC_KEY_NONE;
            return 1;
        }

        if (event.type == SDL_EVENT_QUIT) {
            g_cursor_visible = 1;
            g_cursor_blink_epoch = SDL_GetTicks();
            *key = VIEWBBC_KEY_QUIT;
            return 1;
        }

        if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
            /* Keep the apparent VIEW/font size consistent when crossing displays. */
            rescale_window_for_display();
            *key = VIEWBBC_KEY_NONE;
            return 1;
        }

        if (event.type == SDL_EVENT_WINDOW_RESIZED ||
            event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            update_grid_from_window();
            *key = VIEWBBC_KEY_NONE;
            return 1;
        }

        if (mouse && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button >= 1 && event.button.button <= 6 &&
            mouse_cell_from_window(event.button.x, event.button.y,
                                   &mouse->cell_x, &mouse->cell_y)) {
            g_cursor_visible = 1;
            g_cursor_blink_epoch = SDL_GetTicks();
            mouse->button = (int)event.button.button;
            mouse->clicks = (int)event.button.clicks;
            mouse->modifiers = physical_from_sdl(SDLK_A, SDL_GetModState()).modifiers;
            if (clipboard_action && mouse->button == SDL_BUTTON_RIGHT && g_config &&
                g_config->mouse_buttons[SDL_BUTTON_RIGHT] == VIEWBBC_MOUSE_NONE) {
                *clipboard_action = run_context_menu(mouse->cell_x, mouse->cell_y);
                return 1;
            }
            mouse->type = VIEWBBC_SDL_MOUSE_DOWN;
            return 1;
        }

        if (mouse && event.type == SDL_EVENT_MOUSE_MOTION &&
            event.motion.state != 0 &&
            mouse_cell_from_window_drag(event.motion.x, event.motion.y,
                                        &mouse->cell_x, &mouse->cell_y)) {
            g_cursor_visible = 1;
            g_cursor_blink_epoch = SDL_GetTicks();
            mouse->type = VIEWBBC_SDL_MOUSE_DRAG;
            for (int b=1;b<=6;++b) if ((event.motion.state & SDL_BUTTON_MASK(b)) != 0) { mouse->button=b; break; }
            mouse->modifiers = physical_from_sdl(SDLK_A, SDL_GetModState()).modifiers;
            return 1;
        }

        if (mouse && event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
            event.button.button >= 1 && event.button.button <= 6 &&
            mouse_cell_from_window(event.button.x, event.button.y,
                                   &mouse->cell_x, &mouse->cell_y)) {
            g_cursor_visible = 1;
            g_cursor_blink_epoch = SDL_GetTicks();
            mouse->type = VIEWBBC_SDL_MOUSE_UP;
            mouse->button = (int)event.button.button;
            mouse->modifiers = physical_from_sdl(SDLK_A, SDL_GetModState()).modifiers;
            return 1;
        }

        if (mouse && event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.y != 0.0f) {
            g_cursor_visible = 1;
            g_cursor_blink_epoch = SDL_GetTicks();
            mouse->type = VIEWBBC_SDL_MOUSE_WHEEL;
            mouse->wheel_steps = event.wheel.y > 0.0f ? 1 : -1;
            return 1;
        }

        if (event.type == SDL_EVENT_TEXT_INPUT) {
            const unsigned char *s = (const unsigned char *)event.text.text;
            if (s && s[0] >= 32 && s[0] <= 126 && s[1] == '\0') {
                g_cursor_visible = 1;
                g_cursor_blink_epoch = SDL_GetTicks();
                *ch = (uint32_t)s[0];
                *is_text = 1;
                return 1;
            }
            continue;
        }

        if (event.type != SDL_EVENT_KEY_DOWN) continue;

        /* Only real BeebView input restarts the blink phase.  In particular,
           ignored mouse-motion/window events must not suppress blinking. */
        g_cursor_visible = 1;
        g_cursor_blink_epoch = SDL_GetTicks();

        SDL_Keycode sym = event.key.key;
        SDL_Keymod mod = event.key.mod;

        /* Desktop shortcuts are normal configurable key bindings.  Clipboard
           operations are converted to frontend actions after mapping so changing
           key.paste/key.clipboard_copy/key.cut/key.select_all in CONFIG really
           changes the shortcut. */
        if (sym == SDLK_F12) { *key = VIEWBBC_KEY_BREAK; return 1; }

        ViewBBCPhysicalKey physical = physical_from_sdl(sym, mod);
        ViewBBCKey mapped = viewbbc_config_key_for_physical(g_config, physical);
        if (mapped != VIEWBBC_KEY_NONE) {
            if (clipboard_action) {
                if (mapped == VIEWBBC_KEY_CLIPBOARD_COPY) {
                    *clipboard_action = VIEWBBC_SDL_CLIPBOARD_COPY; return 1;
                }
                if (mapped == VIEWBBC_KEY_CLIPBOARD_PASTE) {
                    *clipboard_action = VIEWBBC_SDL_CLIPBOARD_PASTE; return 1;
                }
                if (mapped == VIEWBBC_KEY_CLIPBOARD_CUT) {
                    *clipboard_action = VIEWBBC_SDL_CLIPBOARD_CUT; return 1;
                }
                if (mapped == VIEWBBC_KEY_SELECT_ALL) {
                    *clipboard_action = VIEWBBC_SDL_CLIPBOARD_SELECT_ALL; return 1;
                }
            }
            *key = mapped;
            return 1;
        }
        continue;
    }

    return 0;
}

static void dialog_clear(void) {
    SDL_SetRenderDrawColor(g_renderer,0,0,0,255); SDL_RenderClear(g_renderer);
}
static void dialog_text(int x,int y,const char*s,int reverse) {
    for(int i=0;s&&s[i]&&x+i<g_grid_cols;++i){ViewBBCCell cell={(uint8_t)s[i],reverse?VIEWBBC_ATTR_REVERSE:0};draw_cell(x+i,y,cell,0);}
}
static int file_exists_path(const char *path) { FILE *f=fopen(path,"rb"); if(!f)return 0; fclose(f); return 1; }

int viewbbc_sdl3_choose_print_file(char *target, size_t target_size) {
    if (!target || target_size < 8u || !g_renderer) return 0;
    static const char *names[] = { "Text (.txt)", "PDF (.pdf)", "OpenDocument (.odt)" };
    static const char *prefix[] = { "text:", "pdf:", "odt:" };
    static const char *ext[] = { ".txt", ".pdf", ".odt" };
    int selected = 0;
    SDL_Event e;

    for (;;) {
        dialog_clear();
        dialog_text(0, 0, "PRINT TO FILE", 0);
        dialog_text(0, 2, "Choose output type:", 0);
        for (int i = 0; i < 3; ++i) dialog_text(2, 4 + i, names[i], i == selected);
        dialog_text(0, 9, "Up/Down select   Enter accepts   Esc cancels", 0);
        SDL_RenderPresent(g_renderer);
        if (!SDL_WaitEvent(&e)) return 0;
        if (e.type == SDL_EVENT_QUIT) { SDL_PushEvent(&e); return 0; }
        if (e.type != SDL_EVENT_KEY_DOWN) continue;
        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_F12) return 0;
        if (e.key.key == SDLK_UP && selected > 0) --selected;
        else if (e.key.key == SDLK_DOWN && selected < 2) ++selected;
        else if (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER) break;
    }

    char path[VIEWBBC_CONFIG_VALUE_MAX + 1] = {0};
    size_t len = 0;
    for (;;) {
        dialog_clear();
        dialog_text(0, 0, "SAVE PRINT AS", 0);
        dialog_text(0, 2, names[selected], 0);
        dialog_text(0, 4, "Filename:", 0);
        char shown[72];
        size_t start = len > 70u ? len - 70u : 0u;
        snprintf(shown, sizeof(shown), "%s", path + start);
        dialog_text(0, 5, shown, 1);
        dialog_text(0, 8, "Enter saves   Esc cancels", 0);
        SDL_RenderPresent(g_renderer);

        if (!SDL_WaitEvent(&e)) return 0;
        if (e.type == SDL_EVENT_QUIT) { SDL_PushEvent(&e); return 0; }
        if (e.type == SDL_EVENT_TEXT_INPUT) {
            size_t n = strlen(e.text.text);
            if (len + n < sizeof(path)) { memcpy(path + len, e.text.text, n + 1u); len += n; }
            continue;
        }
        if (e.type != SDL_EVENT_KEY_DOWN) continue;
        if (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_F12) return 0;
        if (e.key.key == SDLK_BACKSPACE && len) { path[--len] = '\0'; continue; }
        if (e.key.key != SDLK_RETURN && e.key.key != SDLK_KP_ENTER) continue;
        if (len == 0u) continue;

        size_t elen = strlen(ext[selected]);
        if (len < elen || strcmp(path + len - elen, ext[selected]) != 0) {
            if (len + elen >= sizeof(path)) continue;
            memcpy(path + len, ext[selected], elen + 1u);
            len += elen;
        }
        if (file_exists_path(path)) {
            int yes = 0;
            for (;;) {
                dialog_clear();
                dialog_text(0, 0, "FILE EXISTS", 0);
                dialog_text(0, 2, path, 0);
                dialog_text(0, 4, "Overwrite?  Y/N", 0);
                SDL_RenderPresent(g_renderer);
                if (!SDL_WaitEvent(&e)) return 0;
                if (e.type == SDL_EVENT_QUIT) { SDL_PushEvent(&e); return 0; }
                if (e.type != SDL_EVENT_KEY_DOWN) continue;
                if (e.key.key == SDLK_Y) { yes = 1; break; }
                if (e.key.key == SDLK_N || e.key.key == SDLK_ESCAPE || e.key.key == SDLK_F12) break;
            }
            if (!yes) continue;
        }
        if (strlen(prefix[selected]) + len + 1u > target_size) return 0;
        snprintf(target, target_size, "%s%s", prefix[selected], path);
        return 1;
    }
}
