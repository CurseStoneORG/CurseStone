/*
 * CurseStone | Open Source Server Software
 * Copyright (C) 2026 CurseStoneTeam
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
#include "DebugFlag.h"

static volatile int g_debug_mode = 0;

void mcp_debug_mode_set(bool enabled) {
    g_debug_mode = enabled ? 1 : 0;
}

bool mcp_debug_enabled(void) {
    return g_debug_mode != 0;
}
