#ifndef VIEWBBC_NCURSES_FRONTEND_H
#define VIEWBBC_NCURSES_FRONTEND_H

#include <stdint.h>
#include <stddef.h>
#include "viewbbc/editor.h"
#include "viewbbc/config.h"

int viewbbc_ncurses_init(const ViewBBCConfig *config);
void viewbbc_ncurses_shutdown(void);
void viewbbc_ncurses_render(const ViewBBCScreen *screen);
int viewbbc_ncurses_read_input(ViewBBCKey *key, uint32_t *ch, int *is_text);
int viewbbc_ncurses_choose_print_file(char *target, size_t target_size);

#endif
