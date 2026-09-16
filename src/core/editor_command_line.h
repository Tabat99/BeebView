#ifndef VIEWBBC_EDITOR_COMMAND_LINE_H
#define VIEWBBC_EDITOR_COMMAND_LINE_H
#include <stddef.h>
#include "viewbbc/editor.h"
void viewbbc_editor_command_reset_navigation(ViewBBCEditor *editor);
void viewbbc_editor_command_history_add(ViewBBCEditor *editor);
void viewbbc_editor_command_history_up(ViewBBCEditor *editor);
void viewbbc_editor_command_history_down(ViewBBCEditor *editor);
int viewbbc_editor_command_insert_bytes(ViewBBCEditor *editor, size_t at, const char *text, size_t length);
void viewbbc_editor_command_complete(ViewBBCEditor *editor);
void viewbbc_editor_command_clear_selection(ViewBBCEditor *editor);
int viewbbc_editor_command_has_selection(const ViewBBCEditor *editor);
int viewbbc_editor_command_delete_selection(ViewBBCEditor *editor);
void viewbbc_editor_command_select_all(ViewBBCEditor *editor);
void viewbbc_editor_command_select_left(ViewBBCEditor *editor);
void viewbbc_editor_command_select_right(ViewBBCEditor *editor);
size_t viewbbc_editor_command_visible_start(const ViewBBCEditor *editor);
void viewbbc_editor_command_mouse_down(ViewBBCEditor *editor, int screen_x, int screen_y);
void viewbbc_editor_command_mouse_drag(ViewBBCEditor *editor, int screen_x, int screen_y);
void viewbbc_editor_command_mouse_up(ViewBBCEditor *editor, int screen_x, int screen_y);
#endif
