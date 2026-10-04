// SPDX-License-Identifier: LGPL-3.0-or-later
// VillagePalette — the biome-independent block vocabulary of the village
// templates plus the per-biome resolver that turns it into legacy blocks.
//
// The CurseStone-Structures village exports speak modern Java block
// names, and — like vanilla's StructureVillagePieces — the SAME template
// must build with a DIFFERENT material set depending on the village
// biome: a plains village is cobblestone and oak wood with gravel/paths,
// a desert village is the sandstone family with sand where the plains
// one has grass. This file is that mapping, the equivalent of vanilla's
// getBiomeSpecificBlockState(), hand-tuned to what MCPE 0.15.x has.
//
// Where a block kind has no direct desert equivalent in 0.15 the
// substitution is documented inline: farmland and grass become sand (a
// desert farm falls back to the sand that vanilla pushes around its
// crops — 0.15 has no desert crops), gravel path becomes sand, and the
// sandstone stairs vanilla would use fall back to cobble stairs, the
// same substitution the desert temple makes.
//
// Facings inside the specs are the JAVA compass (1 east, 2 west, 3
// south, 4 north, 5 up); resolve() receives the rotation the template
// is being stamped with and turns the facing with it, so doors still
// face their opening after a 90-degree turn.
#pragma once
#include "block/BlockIds.h"
#include "world/generator/biome/Biome.h"
#include "world/generator/structure/Structure.h"
#include <cstdint>

namespace cursestone::world::generator::structure::village {

using cursestone::block::BlockId;

// ---- The vocabulary -------------------------------------------------------
enum SpecKind : uint8_t {
    SPEC_AIR            = 0,
    SPEC_COBBLE,        // cobblestone walls/frames; desert: smooth sandstone
    SPEC_LOG,           // log pillars/beams; desert: smooth sandstone pillar
    SPEC_PLANKS,        // plank floors/walls; desert: cut sandstone
    SPEC_DOUBLE_SLAB,   // double stone slab floor; desert: double sandstone
    SPEC_SLAB,          // stone slab step; desert: sandstone slab
    SPEC_WOOD_STAIRS,   // oak stairs; desert: cobble stairs (no sand stairs in 0.15)
    SPEC_COBBLE_STAIRS, // cobble stairs, both biomes
    SPEC_FENCE,         // oak fence, both biomes
    SPEC_DOOR,          // wooden door, both biomes
    SPEC_PLATE,         // wooden pressure plate
    SPEC_STONE_PLATE,   // stone pressure plate
    SPEC_TORCH,         // standing torch (facing 5)
    SPEC_WALL_TORCH,    // wall torch
    SPEC_LADDER,        // ladder
    SPEC_GLASS_PANE,    // glass pane, both biomes
    SPEC_BOOKSHELF,     // bookshelf, both biomes
    SPEC_CRAFTING,      // crafting table
    SPEC_CHEST,         // chest (facing = which way the front points)
    SPEC_FURNACE,       // furnace (facing = which way the front points)
    SPEC_IRON_BARS,     // iron bars, both biomes
    SPEC_WOOL,          // wool (misc = legacy colour)
    SPEC_GRASS,         // grass block; desert: sand
    SPEC_DIRT,          // dirt; desert: sand
    SPEC_FARMLAND,      // farmland (aux = moisture); desert: sand
    SPEC_WATER,         // still water (misc = level)
    SPEC_LAVA,          // lava (misc = level)
};

// ---- The per-biome material sets ------------------------------------------
//
// Which material family a village in a given biome builds with. SAVANNA
// uses the plains set: vanilla 1.9-era savanna villages reuse the plains
// pieces with acacia accents, and acacia wood/planks only exist as LOG
// data 1 / 125 data 1 — close enough to oak to not be worth splitting
// the palette for, while the shapes and layout stay vanilla.
inline bool isDesertVillageBiome(uint8_t biomeId) {
    return biomeId == (uint8_t)cursestone::world::generator::biome::BIOME_DESERT;
}

// Legacy data for the JAVA compass values (1 east, 2 west, 3 south,
// 4 north), per block family:
//
//   * stairs (legacy 53/67): 0 east, 1 west, 2 south, 3 north + 4 top.
//     Vanilla legacy stairs face TOWARD the player: the export's value
//     is already that, so it transfers 1:1.
//   * doors: 0 east, 1 south, 2 west, 3 north (bits 0-1), + 4 hinge
//     right, + 8 open. The upper half stores nothing directional.
//   * wall torch: 1 west, 2 east, 3 south, 4 north — the same map the
//     desert temple used (verified there against the client).
//   * chest/furnace/ladder (side-mounted): each family has its OWN
//     table — a ladder's data is the side its face touches (2 north,
//     3 south, 4 west, 5 east), while a chest/furnace's data is its
//     front direction in 0..3 (0 north, 1 south, 2 west, 3 east).
//   * standing torch: 5.
constexpr uint8_t kStairsFacing[5]   = {0, 0, 1, 2, 3};  // idx = java facing
constexpr uint8_t kDoorFacing[5]     = {0, 0, 2, 1, 3};
constexpr uint8_t kWallTorchFacing[5]= {0, 2, 1, 3, 4};
// Ladder: legacy data is the side the ladder's face touches
// (2 north, 3 south, 4 west, 5 east).
constexpr uint8_t kLadderFacing[5]   = {0, 5, 4, 3, 2};
// Chest/furnace: legacy data is the FRONT direction in 0..3
// (0 north, 1 south, 2 west, 3 east) — a different table from the
// ladder's, and writing a ladder value here would spawn a double chest.
constexpr uint8_t kFrontFacing[5]    = {0, 3, 2, 1, 0};

// The spec facings are the JAVA compass (1 east, 2 west, 3 south, 4
// north), so they index the tables directly — masking with 3 here would
// silently turn north (4) into east (0).
inline uint8_t stairsData(const BlockSpec& s) {
    return (uint8_t)(kStairsFacing[s.facing] | (s.aux & 1 ? 4 : 0));
}
inline uint8_t doorData(const BlockSpec& s) {
    const bool upper = (s.aux & 1) != 0;
    const uint8_t bits = (uint8_t)(kDoorFacing[s.facing] |
                                   (s.misc & 1 ? 4 : 0));  // open
    return upper ? (uint8_t)(bits | 8) : bits;
}

// The resolver Structure::placeInto calls per cell. `user` carries the
// village biome id (0 = not a village biome, resolves as plains).
inline BlockState resolve(const BlockSpec& s, int rot, void* user) {
    const uint8_t biomeId = user ? *(const uint8_t*)user : 0;
    const bool desert = isDesertVillageBiome(biomeId);
    // Facings rotate WITH the template (rotation-invariant kinds just
    // pass through; the door's hinge/open bits ride along untouched).
    const uint8_t facing =
        (s.facing <= 4 && (rot & 3) != 0) ? rotateFacing(s.facing, rot)
                                          : s.facing;
    const BlockSpec t{s.kind, facing, s.aux, s.misc};

    switch (s.kind) {
        case SPEC_AIR:          return {0, 0};
        case SPEC_COBBLE:       return desert ? BlockState{(uint8_t)BlockId::SANDSTONE, 0}
                                              : BlockState{(uint8_t)BlockId::COBBLESTONE, 0};
        case SPEC_LOG:          return desert ? BlockState{(uint8_t)BlockId::SANDSTONE, 0}
                                              : BlockState{(uint8_t)BlockId::LOG, 0};
        case SPEC_PLANKS:       return desert ? BlockState{(uint8_t)BlockId::SANDSTONE, 2}
                                              : BlockState{(uint8_t)BlockId::PLANKS, 0};
        case SPEC_DOUBLE_SLAB:  return desert ? BlockState{43, 1}
                                              : BlockState{43, 0};
        case SPEC_SLAB:         return desert ? BlockState{(uint8_t)BlockId::STONE_SLAB,
                                                           (uint8_t)(1 | (t.aux & 1 ? 8 : 0))}
                                              : BlockState{(uint8_t)BlockId::STONE_SLAB,
                                                           (uint8_t)(3 | (t.aux & 1 ? 8 : 0))};
        case SPEC_WOOD_STAIRS:  return desert ? BlockState{(uint8_t)BlockId::COBBLE_STAIRS, stairsData(t)}
                                              : BlockState{(uint8_t)BlockId::WOOD_STAIRS, stairsData(t)};
        case SPEC_COBBLE_STAIRS:return {(uint8_t)BlockId::COBBLE_STAIRS, stairsData(t)};
        case SPEC_FENCE:        return {(uint8_t)BlockId::FENCE, 0};
        case SPEC_DOOR:         return {(uint8_t)BlockId::WOODEN_DOOR, doorData(t)};
        case SPEC_PLATE:        return {(uint8_t)BlockId::WOOD_PRESSURE_PLATE, 0};
        case SPEC_STONE_PLATE:  return {(uint8_t)BlockId::STONE_PRESSURE_PLATE, 0};
        case SPEC_TORCH:        return {(uint8_t)BlockId::TORCH, 5};
        case SPEC_WALL_TORCH:   return {(uint8_t)BlockId::TORCH, kWallTorchFacing[t.facing]};
        case SPEC_LADDER:       return {(uint8_t)BlockId::LADDER, kLadderFacing[t.facing]};
        case SPEC_GLASS_PANE:   return {(uint8_t)BlockId::GLASS_PANE, 0};
        case SPEC_BOOKSHELF:    return {(uint8_t)BlockId::BOOKSHELF, 0};
        case SPEC_CRAFTING:     return {(uint8_t)BlockId::WORKBENCH, 0};
        case SPEC_CHEST:        return {(uint8_t)BlockId::CHEST, kFrontFacing[t.facing]};
        case SPEC_FURNACE:      return {(uint8_t)BlockId::FURNACE, kFrontFacing[t.facing]};
        case SPEC_IRON_BARS:    return {(uint8_t)BlockId::IRON_BARS, 0};
        case SPEC_WOOL:         return {(uint8_t)BlockId::WOOL, t.misc};
        case SPEC_GRASS:        return desert ? BlockState{(uint8_t)BlockId::SAND, 0}
                                              : BlockState{(uint8_t)BlockId::GRASS, 0};
        case SPEC_DIRT:         return desert ? BlockState{(uint8_t)BlockId::SAND, 0}
                                              : BlockState{(uint8_t)BlockId::DIRT, 0};
        case SPEC_FARMLAND:     return desert ? BlockState{(uint8_t)BlockId::SAND, 0}
                                              : BlockState{(uint8_t)BlockId::FARMLAND, t.aux};
        case SPEC_WATER:        return {(uint8_t)BlockId::WATER, t.misc};
        case SPEC_LAVA:         return {(uint8_t)BlockId::LAVA, t.misc};
        default:                return {0, 0};
    }
}

} // namespace cursestone::world::generator::structure::village
