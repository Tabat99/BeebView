#include "terminal_launch.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define VIEWBBC_LAUNCH_ARG_MAX 128

static int has_terminal(void) {
    return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

static int copy_original_args(char **dst,
                              size_t dst_count,
                              size_t prefix_count,
                              int argc,
                              char *argv[]) {
    size_t needed;

    if (argc < 1 || argv == NULL || argv[0] == NULL) return 0;
    if ((size_t)argc > SIZE_MAX - prefix_count - 1u) return 0;

    needed = prefix_count + (size_t)argc + 1u;
    if (needed > dst_count) return 0;

    for (int i = 0; i < argc; ++i) {
        dst[prefix_count + (size_t)i] = argv[i];
    }
    dst[prefix_count + (size_t)argc] = NULL;
    return 1;
}

static void try_exec_x_terminal_emulator(int argc, char *argv[]) {
    char *args[VIEWBBC_LAUNCH_ARG_MAX];

    args[0] = (char *)"x-terminal-emulator";
    args[1] = (char *)"-e";
    if (!copy_original_args(args, VIEWBBC_LAUNCH_ARG_MAX, 2u, argc, argv)) return;
    execvp(args[0], args);
}

static void try_exec_xdg_terminal(int argc, char *argv[]) {
    char *args[VIEWBBC_LAUNCH_ARG_MAX];

    args[0] = (char *)"xdg-terminal-exec";
    args[1] = (char *)"--";
    if (!copy_original_args(args, VIEWBBC_LAUNCH_ARG_MAX, 2u, argc, argv)) return;
    execvp(args[0], args);
}

static void try_exec_gnome_terminal(int argc, char *argv[]) {
    char *args[VIEWBBC_LAUNCH_ARG_MAX];

    args[0] = (char *)"gnome-terminal";
    args[1] = (char *)"--";
    if (!copy_original_args(args, VIEWBBC_LAUNCH_ARG_MAX, 2u, argc, argv)) return;
    execvp(args[0], args);
}

static void try_exec_konsole(int argc, char *argv[]) {
    char *args[VIEWBBC_LAUNCH_ARG_MAX];

    args[0] = (char *)"konsole";
    args[1] = (char *)"-e";
    if (!copy_original_args(args, VIEWBBC_LAUNCH_ARG_MAX, 2u, argc, argv)) return;
    execvp(args[0], args);
}

int viewbbc_terminal_prepare(int argc, char *argv[]) {
    const char *disable_relaunch;

    if (has_terminal()) return 1;

    /* Useful for automated/headless runs which intentionally have no tty. */
    disable_relaunch = getenv("VIEWBBC_TERMINAL_NO_RELAUNCH");
    if (disable_relaunch != NULL && disable_relaunch[0] != '\0') {
        return 0;
    }

    if (argc < 1 || argv == NULL || argv[0] == NULL || argv[0][0] == '\0') {
        return 0;
    }

    /*
     * Linux Mint/Debian normally provides x-terminal-emulator.  The other
     * launchers cover common desktops and the emerging XDG helper.  execvp()
     * is used directly: no shell is involved, so paths/arguments are not
     * subject to shell expansion or command injection.
     */
    try_exec_x_terminal_emulator(argc, argv);
    try_exec_xdg_terminal(argc, argv);
    try_exec_gnome_terminal(argc, argv);
    try_exec_konsole(argc, argv);

    fprintf(stderr,
            "ViewBBC Terminal needs a terminal window, and no supported terminal "
            "launcher was found.\n"
            "Install/configure x-terminal-emulator, xdg-terminal-exec, "
            "gnome-terminal or konsole.\n");
    return 0;
}
