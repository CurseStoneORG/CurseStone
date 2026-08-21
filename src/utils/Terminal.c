// SPDX-License-Identifier: LGPL-3.0-or-later
// Terminal.c — tty detection. Pure C.
#include "Terminal.h"

#include <stdbool.h>

#ifdef _WIN32
#  include <io.h>
#  define IS_TTY_FD(fd) _isatty(fd)
#else
#  include <unistd.h>
#  define IS_TTY_FD(fd) isatty(fd)
#endif

static bool g_color_enabled = false;

void mcp_terminal_init(bool force_color) {
    g_color_enabled = force_color || (IS_TTY_FD(1) != 0);
}

bool mcp_terminal_has_color(void) {
    return g_color_enabled;
}
