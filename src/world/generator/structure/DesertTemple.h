// SPDX-License-Identifier: LGPL-3.0-or-later
// DesertTemple — the vanilla desert pyramid, as a structure.
//
// The block table lives in DesertTemple.cpp, converted from the JSON
// export in CurseStoneTeam/CurseStone-Structures.
#pragma once
#include "Structure.h"

namespace cursestone::level::generator::structure {

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

} // namespace cursestone::level::generator::structure