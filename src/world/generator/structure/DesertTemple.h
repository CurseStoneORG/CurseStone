/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */

// SPDX-License-Identifier: LGPL-3.0-or-later
// DesertTemple — the vanilla desert pyramid, as a structure.
//
// The block table lives in DesertTemple.cpp, converted from the JSON
// export in CurseStoneTeam/CurseStone-Structures.
#pragma once
#include "Structure.h"

namespace cursestone::world::generator::structure {

// Vanilla dimensions of the export: 11 wide, 7 tall, 10 deep.
inline constexpr int kDesertTempleSizeX = 11;
inline constexpr int kDesertTempleSizeY = 7;
inline constexpr int kDesertTempleSizeZ = 10;

class DesertTemple final : public Structure {
  public:
    DesertTemple();

    // Vanilla desert pyramids only generate in the desert biome (the
    // plains-like desert hills are NOT one of them, and the request was
    // for desert only).
    bool allowsBiome(uint8_t biome) const override;
};

} // namespace cursestone::world::generator::structure