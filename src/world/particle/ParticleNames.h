/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// ParticleNames — the protocol-84 particle name registry shared by the
// Lua and native plugin APIs. Plugins address particles by name (Lua) or
// by the ids of ParticleId (native); this header is the single authority
// that maps between the two. The id space itself lives in ParticleId.h.
#pragma once
#include "ParticleId.h"
#include <string_view>
#include <cstdint>

namespace cursestone::world::particle {

/// @brief Looks up a particle type id by its API name.
///
/// Names are lower snake_case, e.g. "smoke", "large_explode", "terrain",
/// "mob_spell_ambient". Lookup is exact and case-sensitive.
///
/// @param name   candidate particle name.
/// @param out_id receives the particle type id on success, untouched on failure.
/// @return true when @p name is a known particle name.
bool particleFromName(std::string_view name, int32_t& out_id);

} // namespace cursestone::world::particle
