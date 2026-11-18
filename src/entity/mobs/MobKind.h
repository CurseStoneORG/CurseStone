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
// MobKind — the behavior family of a registered mob. Every per-mob
// header in mobs/ declares its kind, and the plugin APIs expose it so
// scripts and native plugins can group mobs without hardcoding ids.
#pragma once
#include <cstdint>
namespace cursestone::entity::mobs {

/// @brief The behavior family of a mob.
enum class MobKind : uint8_t {
    Unknown, ///< not registered or not yet classified
    Passive, ///< never attacks players
    Neutral, ///< retaliates only once provoked
    Hostile, ///< attacks players on sight
    Water,   ///< aquatic mob
    Ambient, ///< ambient mob
};

} // namespace cursestone::entity::mobs
