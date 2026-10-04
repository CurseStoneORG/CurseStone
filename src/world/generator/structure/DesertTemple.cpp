// SPDX-License-Identifier: LGPL-3.0-or-later
#include "DesertTemple.h"
#include "world/generator/biome/Biome.h"
#include "block/BlockIds.h"

namespace cursestone::world::generator::structure {

namespace {

using cursestone::block::BlockId;

// ---- Palette ------------------------------------------------------------
//
// The export speaks MODERN JAVA block names ("minecraft:smooth_sandstone",
// "minecraft:white_glazed_terracotta", slab/stairs/torch `facing`
// properties, ...) while the chunk stores the MCPE 0.15.x legacy id plus
// one metadata byte. Every entry below is therefore a hand mapping from
// the Java name+state onto that legacy (id, data) pair:
//
//   * cut / smooth / chiseled sandstone are three metadata values of the
//     single legacy SANDSTONE block, not three blocks.
//   * glazed terracotta does not exist in 0.15; white terracotta does
//     (legacy 159, "stained hardened clay", colour 0 = white). The same
//     substitution covers the jigsaw block, which is a plain light block
//     in the temple's trim.
//   * potted cactus becomes a flower pot (legacy 140), which is the same
//     "small plant on a block" read in 0.15.
//   * stairs are legacy 67 (stone stairs); bits 0-1 are the facing
//     (0 east, 1 west, 2 south, 3 north) and bit 2 is "top half".
//   * slabs are legacy 44 (stone slab); metadata 1 is sandstone and bit 3
//     (value 8) is the top half.
//   * wall torches are legacy 50 with facing 1 west, 2 east, 3 south,
//     4 north.
//
// The chest keeps its facing so its lid does not open into a wall. It is
// placed as a plain block: the export carries no loot, and CurseStone has
// no structure loot table yet, so the chest comes up empty.
//
// Entries are in volume order: index 0 is the character 'a', index 19 is
// 't'.
constexpr BlockState kPalette[] = {
    {(uint8_t)BlockId::SANDSTONE,     2},     // a  minecraft:cut_sandstone
    {(uint8_t)BlockId::SANDSTONE,     0},     // b  minecraft:smooth_sandstone
    {(uint8_t)BlockId::CLAY_STAINED,  0},     // c  minecraft:white_glazed_terracotta (north)
    {(uint8_t)BlockId::CLAY_STAINED,  0},     // d  minecraft:white_glazed_terracotta (east)
    {(uint8_t)BlockId::CLAY_STAINED,  0},     // e  minecraft:white_glazed_terracotta (west)
    {(uint8_t)BlockId::CLAY_STAINED,  0},     // f  minecraft:white_glazed_terracotta (south)
    {(uint8_t)BlockId::BREWING_STAND, 0},     // g  minecraft:brewing_stand
    {(uint8_t)BlockId::CLAY_STAINED,  0},     // h  minecraft:jigsaw (facing west)
    {(uint8_t)BlockId::CHEST,         1},     // i  minecraft:chest (facing east)
    {0,                                 0},     // j  minecraft:air
    {(uint8_t)BlockId::FLOWER_POT,    0},     // k  minecraft:potted_cactus
    {(uint8_t)BlockId::STONE_SLAB,    1 | 8}, // l  minecraft:sandstone_slab (top half)
    {(uint8_t)BlockId::STONE_SLAB,    1},     // m  minecraft:smooth_sandstone_slab (bottom)
    {(uint8_t)BlockId::STONE_SLAB,    1},     // n  minecraft:sandstone_slab (bottom)
    {(uint8_t)BlockId::TORCH,         3},     // o  minecraft:wall_torch (facing south)
    {(uint8_t)BlockId::TORCH,         1},     // p  minecraft:wall_torch (facing west)
    {(uint8_t)BlockId::COBBLE_STAIRS, 0},     // q  minecraft:smooth_sandstone_stairs (bottom, east)
    {(uint8_t)BlockId::COBBLE_STAIRS, 3 | 4}, // r  minecraft:smooth_sandstone_stairs (top, north)
    {(uint8_t)BlockId::COBBLE_STAIRS, 2 | 4}, // s  minecraft:smooth_sandstone_stairs (top, south)
    {(uint8_t)BlockId::TORCH,         4},     // t  minecraft:wall_torch (facing north)
};
static_assert(sizeof(kPalette) / sizeof(kPalette[0]) == 20, "20 palette entries");

// ---- Volume -------------------------------------------------------------
//
// 11 x 7 x 10 = 770 cells, air included, exactly as exported. Rows run
// z = 0..9 inside each y, each row is x = 0..10 left to right, so cell
// (lx, ly, lz) is character (ly * 10 + lz) * 11 + lx of the string, and
// character 'a' + n is palette entry n.
//
// The rows are separate string literals so the shape stays readable; they
// are concatenated by the compiler, which is also what the length check
// below verifies.
constexpr char kVolume[] =
    // y = 0 -- the floor
    "aaajjjjjaaa"  // z = 0
    "abbaaaaabba"  // z = 1
    "abbbbbbbbba"  // z = 2
    "jabbbbbbbaj"  // z = 3
    "jjjqbcdbbaj"  // z = 4
    "hjjqbefbbaj"  // z = 5
    "jabbbbbbbaj"  // z = 6
    "abbbbbbbbba"  // z = 7
    "abbaaaaabba"  // z = 8
    "aaajjjjjaaa"  // z = 9
    // y = 1
    "bbbjjjjjbbb"  // z = 0
    "bgjbbbbbjjb"  // z = 1
    "bjjjjjjjjjb"  // z = 2
    "jbjjjjjjjbj"  // z = 3
    "jjjjjjjjjbj"  // z = 4
    "jjjjjjjjjbj"  // z = 5
    "jbjjjjjjjbj"  // z = 6
    "bjjjjjjjjjb"  // z = 7
    "bijbbbbbjjb"  // z = 8
    "bbbjjjjjbbb"  // z = 9
    // y = 2
    "bkbjjjjjbkb"  // z = 0
    "kjjbbbbbjjk"  // z = 1
    "bjjjojojjjb"  // z = 2
    "pbjjjjjjjbj"  // z = 3
    "jrjjjjjjpbj"  // z = 4
    "jsjjjjjjpbj"  // z = 5
    "pbjjjjjjjbj"  // z = 6
    "bjjjtjtjjjb"  // z = 7
    "kjjbbbbbjjk"  // z = 8
    "bkbjjjjjbkb"  // z = 9
    // y = 3
    "blbjjjjjblb"  // z = 0
    "ljjbbbbbjjl"  // z = 1
    "bjjjjjjjjjb"  // z = 2
    "jbjjjjjjjbj"  // z = 3
    "jbjjjjjjjbj"  // z = 4
    "jbjjjjjjjbj"  // z = 5
    "jbjjjjjjjbj"  // z = 6
    "bjjjjjjjjjb"  // z = 7
    "ljjbbbbbjjl"  // z = 8
    "bjbjjjjjblb"  // z = 9
    // y = 4 -- the slab ring that floors the side chambers
    "bbbjjjjjbbb"  // z = 0
    "bjbnnnnnbjb"  // z = 1
    "bbbnnnnnbbb"  // z = 2
    "jnnnmnmnnnj"  // z = 3
    "jnnnnmnnnnj"  // z = 4
    "jnnnnmnnnnj"  // z = 5
    "jnnnmnmnnnj"  // z = 6
    "bbbnnnnnbbb"  // z = 7
    "bjbnnnnnbjb"  // z = 8
    "bbbjjjjjbbb"  // z = 9
    // y = 5
    "bmbjjjjjbmb"  // z = 0
    "mnmjjjjjmnm"  // z = 1
    "bmbjjjjjbmb"  // z = 2
    "jjjjjjjjjjj"  // z = 3
    "jjjjjjjjjjj"  // z = 4
    "jjjjjjjjjjj"  // z = 5
    "jjjjjjjjjjj"  // z = 6
    "bmbjjjjjbmb"  // z = 7
    "mnmjjjjjmnm"  // z = 8
    "bmbjjjjjbmb"  // z = 9
    // y = 6 -- the stepped roof corners
    "njnjjjjjnjn"  // z = 0
    "jjjjjjjjjjj"  // z = 1
    "njnjjjjjnjn"  // z = 2
    "jjjjjjjjjjj"  // z = 3
    "jjjjjjjjjjj"  // z = 4
    "jjjjjjjjjjj"  // z = 5
    "jjjjjjjjjjj"  // z = 6
    "njnjjjjjnjn"  // z = 7
    "jjjjjjjjjjj"  // z = 8
    "njnjjjjjnjn"  // z = 9
    "";
static_assert(sizeof(kVolume) - 1 ==
                  (size_t)(kDesertTempleSizeX * kDesertTempleSizeY * kDesertTempleSizeZ),
              "the volume must cover every cell of the bounding box");

} // namespace

DesertTemple::DesertTemple()
    : Structure("DesertTemple",
                kDesertTempleSizeX, kDesertTempleSizeY, kDesertTempleSizeZ,
                kPalette, sizeof(kPalette) / sizeof(kPalette[0]),
                kVolume) {}

bool DesertTemple::allowsBiome(uint8_t biome) const {
    return biome == biome::BIOME_DESERT;
}

} // namespace cursestone::world::generator::structure