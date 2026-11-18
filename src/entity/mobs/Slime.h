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
// Slime — protocol-84 mob id 37 ("slime"). Hostile mob that hatches from its own creative spawn egg.
#pragma once
#include "MobType.h"
namespace cursestone::entity::mobs {

/// @brief Registry descriptor for the slime (protocol-84 id 37).
struct Slime : MobType {
    static constexpr int32_t kId = 37;
    static constexpr const char* kName = "slime";
    static constexpr MobKind kKind = MobKind::Hostile;
};

} // namespace cursestone::entity::mobs
