#pragma once
/*
 * CurseStone | Open Source Server Software
 * Copyright (C) 2026 CurseStoneTeam
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
/**
 * @brief Global debug-mode switch shared by every layer (C and C++).
 *
 * Source of truth: the `settings.debug-mode` key in cursestone.yml
 * (default false). Server::loadConfig() pushes the parsed value here
 * once at boot; every scattered debug printer — the RakLib [NET]
 * diagnostics on the network thread, the packet hexdumps, the verbose
 * gameplay traces — checks mcp_debug_enabled() instead of having its
 * own flag, so ONE key silences or reveals all of it.
 *
 * Plain C on purpose: SessionManager.c (RakLib thread) includes this
 * and the async logger is C as well. The read is a relaxed atomic
 * int; it is written once at startup before any worker exists.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void mcp_debug_mode_set(bool enabled);
bool mcp_debug_enabled(void);

#ifdef __cplusplus
} // extern "C"
#endif
