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
// VillagePieces — the 1.9-era village piece set as code.
//
// The templates live in VillagePieces.cpp (GENERATED from the
// CurseStone-Structures village exports by tools/gen_village_pieces.py);
// this header is the stable API the populator talks to: the piece kind
// list, the concrete Structure subclass that carries a spec palette,
// and the accessor that hands pieces out.
//
// The piece set is the full vanilla old-village inventory: the well,
// four farms (well-adjacent small + large), five house tiers, the
// library, the church, the blacksmith, the butcher shop and the street
// lamp. VillagePopulator assembles them around the well the way
// StructureVillagePieces.Start does — center building, roads on the
// four cardinal axes, houses branching off the roads.
#pragma once
#include "world/generator/structure/Structure.h"
#include "world/generator/structure/village/VillagePalette.h"

namespace cursestone::world::generator::structure::village {

// One concrete structure carrying a BlockSpec palette. The per-biome
// material choice happens in VillagePalette::resolve at placement time;
// the template itself is biome-neutral.
//
// frontFacing is the java compass (1 east, 2 west, 3 south, 4 north)
// the piece's ENTRANCE door opens toward once placed — the door data
// itself faces that way (the exports store the inward facing, which
// gen_village_pieces.py flips). 0 means the export has no outside door
// (the tower houses, the farms, the lamp and the well), and such a
// piece is rotated freely.
class VillagePiece final : public Structure {
  public:
    VillagePiece(const char* name, int sizeX, int sizeY, int sizeZ, const BlockSpec* specs,
                 size_t specCount, const char* volume, int frontFacing)
        : Structure(name, sizeX, sizeY, sizeZ, specs, specCount, volume),
          frontFacing_(frontFacing) {
    }

    int frontFacing() const {
        return frontFacing_;
    }

  private:
    int frontFacing_;
};

// Alphabetical order matches the generated piece table in
// VillagePieces.cpp — keep both in sync (the table is regenerated).
enum PieceKind : uint8_t {
    PIECE_BLACKSMITH = 0,
    PIECE_BUTCHER,
    PIECE_CHURCH,
    PIECE_LIBRARY,
    PIECE_STREETLIGHT,
    PIECE_T1_FARM,
    PIECE_T1_HOUSE,
    PIECE_T2_FARM,
    PIECE_T2_HOUSE,
    PIECE_T3_HOUSE,
    PIECE_T4_HOUSE,
    PIECE_T5_HOUSE,
    PIECE_WELL,
    PIECE_COUNT
};

// The pieces live for the whole process (static storage), so this hands
// out a raw pointer.
const Structure* piece(PieceKind k);

// Java compass (1 east, 2 west, 3 south, 4 north) the piece's entrance
// door opens toward once placed; 0 when the export has no outside door.
int pieceFrontFacing(PieceKind k);

inline int pieceCount() {
    return PIECE_COUNT;
}

} // namespace cursestone::world::generator::structure::village
