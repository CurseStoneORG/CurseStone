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
// MobType — the shared contract of the per-mob descriptors in mobs/.
// Each mob file derives from this struct and overrides the constants
// it declares; EntityTypes aggregates the derivations into the runtime
// registry, so a mob's own file is the single authority for its id,
// name, aliases and kind.
#pragma once
#include "MobKind.h"
#include <cstddef>
#include <cstdint>
namespace cursestone::entity::mobs {

/// @brief Shared contract of the per-mob descriptor headers.
struct MobType {
    static constexpr int32_t kId = 0;        ///< Protocol-84 network id.
    static constexpr const char* kName = ""; ///< Canonical lowercase name.
    static constexpr MobKind kKind = MobKind::Unknown; ///< Behavior family.
    static constexpr const char* const* kAliases = nullptr; ///< Extra accepted names.
    static constexpr size_t kAliasCount = 0; ///< Entries in kAliases.
};

} // namespace cursestone::entity::mobs
