#ifndef VIEWBBC_TERMINAL_LAUNCH_H
#define VIEWBBC_TERMINAL_LAUNCH_H

/*
 * Return non-zero when stdin/stdout are attached to a terminal and ncurses
 * can start normally.  When launched without a terminal (for example by
 * double-clicking viewbbc-terminal in a file manager), this function attempts
 * to replace the current process with a terminal emulator which runs the same
 * executable.  A successful relaunch therefore does not return.
 */
int viewbbc_terminal_prepare(int argc, char *argv[]);

#endif
