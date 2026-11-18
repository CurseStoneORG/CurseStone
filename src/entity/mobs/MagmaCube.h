/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

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
// MagmaCube — protocol-84 mob id 42 ("magma_cube"). Hostile Nether mob; id 42 is the renamed magma cube id, carried by its own creative egg.
#pragma once
#include "MobType.h"
namespace cursestone::entity::mobs {

/// @brief Registry descriptor for the magma_cube (protocol-84 id 42).
struct MagmaCube : MobType {
    static constexpr int32_t kId = 42;
    static constexpr const char* kName = "magma_cube";
    static constexpr MobKind kKind = MobKind::Hostile;
    static constexpr const char* kAliases[] = {"magma"};
    static constexpr size_t kAliasCount = 1;
};

} // namespace cursestone::entity::mobs
