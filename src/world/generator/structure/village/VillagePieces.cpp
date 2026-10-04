// SPDX-License-Identifier: LGPL-3.0-or-later
// VillagePieces.cpp — the village templates as code. GENERATED from the
// CurseStone-Structures village exports by tools/gen_village_pieces.py
// (regenerate with `python3 tools/gen_village_pieces.py <repo checkout>`);
// hand edits will be overwritten.
//
// Every palette entry is a biome-independent BlockSpec: the SAME template
// builds in plains (oak/cobble, the export's original materials) and in
// desert (the sandstone family) through VillagePalette::resolve — vanilla's
// getBiomeSpecificBlockState. Volumes are dense (air included), row-major
// (z, y) with x fastest, 'a' + n = spec n.
#include "world/generator/structure/village/VillagePieces.h"

namespace cursestone::level::generator::structure::village {

namespace {
// ---- blacksmith (8x6x10, 16 specs) ----
constexpr BlockSpec kSpecs0[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_LOG, 0, 0, 0},
    {SPEC_PLANKS, 0, 0, 0},
    {SPEC_DOUBLE_SLAB, 0, 0, 0},
    {SPEC_CHEST, 4, 0, 0},
    {SPEC_FURNACE, 2, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_SLAB, 0, 0, 0},
    {SPEC_WOOD_STAIRS, 4, 0, 0},
    {SPEC_FENCE, 0, 0, 0},
    {SPEC_WOOD_STAIRS, 1, 0, 0},
    {SPEC_PLATE, 0, 0, 0},
    {SPEC_COBBLE_STAIRS, 1, 0, 0},
    {SPEC_LAVA, 0, 0, 0},
    {SPEC_IRON_BARS, 0, 0, 0},
};
constexpr char kVolume0[] =
    "gaaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "naaaaaaa"
    "naaaaaaa"
    "naaaaaaa"
    "gaaaaaaa"
    "gbcccccb"
    "gcgggjcc"
    "gcgggklc"
    "gbgcgggc"
    "ggggcggc"
    "gkggcgec"
    "ggggaaaa"
    "gggggaoa"
    "ggdggaoa"
    "gkgggaaa"
    "gbchchcb"
    "gcgggggc"
    "gcgggmgh"
    "gbgcgggc"
    "ggggcggh"
    "gkggcggc"
    "ggggfaaa"
    "ggggggga"
    "ggggggga"
    "gkgggppa"
    "gbcccccb"
    "gcgggggc"
    "gcgggggc"
    "gbccgggc"
    "ggggcggc"
    "gkggcggc"
    "ggggfaaa"
    "gggggaaa"
    "gggggaaa"
    "gkgggaaa"
    "gbaaaaab"
    "gaaaaaaa"
    "gaaaaaaa"
    "gbaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "gaaaaaaa"
    "giiiiiii"
    "gigggggi"
    "gigggggi"
    "gigggggi"
    "gigggggi"
    "gigggggi"
    "gigggggi"
    "gigggggi"
    "gigggggi"
    "giiiiiii"
;

// ---- butcher shop (12x7x9, 17 specs) ----
constexpr BlockSpec kSpecs1[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_LOG, 0, 0, 0},
    {SPEC_PLANKS, 0, 0, 0},
    {SPEC_GRASS, 0, 0, 0},
    {SPEC_DIRT, 0, 0, 0},
    {SPEC_DOUBLE_SLAB, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_WOOD_STAIRS, 1, 0, 0},
    {SPEC_WOOD_STAIRS, 2, 0, 0},
    {SPEC_WOOD_STAIRS, 4, 0, 0},
    {SPEC_FENCE, 0, 0, 0},
    {SPEC_DOOR, 2, 0, 0},
    {SPEC_PLATE, 0, 0, 0},
    {SPEC_DOOR, 2, 1, 0},
    {SPEC_WALL_TORCH, 2, 0, 0},
    {SPEC_WALL_TORCH, 1, 0, 0},
};
constexpr char kVolume1[] =
    "gggggaaaaaag"
    "gggggaccccag"
    "dddddaccccaj"
    "ddddeaccccag"
    "ddeedaccccag"
    "ddeeeacfffag"
    "ddddaacfffag"
    "dddddacfffag"
    "dddddaaaaaag"
    "gggggaaaaaag"
    "gggggackggag"
    "lllllajlggmg"
    "lggggaggggag"
    "lggggaggggag"
    "lggggaggggag"
    "lggggmggffag"
    "lggggaggggag"
    "lllllaaaaaag"
    "gggggabhhbag"
    "gggggcggggcg"
    "ggggghgnggog"
    "ggggghggggcg"
    "gggggcggggcg"
    "gggggcgggghg"
    "gggggoggggcg"
    "gggggcggggcg"
    "gggggabhhbag"
    "ggggiaaaaaaj"
    "ggggicggggcj"
    "ggggicgggpcj"
    "ggggicggggcj"
    "ggggicggggcj"
    "ggggicggggcj"
    "ggggicqgggcj"
    "ggggicggggcj"
    "ggggiaaaaaaj"
    "gggggiccccjg"
    "gggggicggcjg"
    "gggggicggcjg"
    "gggggicggcjg"
    "gggggicggcjg"
    "gggggicggcjg"
    "gggggicggcjg"
    "gggggicggcjg"
    "gggggiccccjg"
    "ggggggiccjgg"
    "ggggggiccjgg"
    "ggggggiccjgg"
    "ggggggiccjgg"
    "ggggggiccjgg"
    "ggggggiccjgg"
    "ggggggiccjgg"
    "ggggggiccjgg"
    "ggggggiccjgg"
    "gggggggijggg"
    "gggggggijggg"
    "gggggggijggg"
    "gggggggijggg"
    "gggggggijggg"
    "gggggggijggg"
    "gggggggijggg"
    "gggggggijggg"
    "gggggggijggg"
;

// ---- church (5x12x10, 13 specs) ----
constexpr BlockSpec kSpecs2[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_COBBLE_STAIRS, 3, 0, 0},
    {SPEC_DOOR, 4, 0, 0},
    {SPEC_DOOR, 4, 1, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_LADDER, 2, 0, 0},
    {SPEC_WALL_TORCH, 3, 0, 0},
    {SPEC_WALL_TORCH, 1, 0, 0},
    {SPEC_WALL_TORCH, 2, 0, 0},
    {SPEC_COBBLE_STAIRS, 2, 0, 0},
    {SPEC_COBBLE_STAIRS, 1, 0, 0},
    {SPEC_WALL_TORCH, 4, 0, 0},
};
constexpr char kVolume2[] =
    "bbcbb"
    "baaab"
    "baaab"
    "baaab"
    "baaab"
    "aaaaa"
    "aaaaa"
    "aaaaa"
    "aaaaa"
    "baaab"
    "bbbbb"
    "badab"
    "abbba"
    "abbba"
    "abbga"
    "abbba"
    "acbca"
    "aacaa"
    "aaaaa"
    "baaab"
    "bbbbb"
    "baeab"
    "abbba"
    "fbbbf"
    "abbga"
    "abbba"
    "abbba"
    "abbba"
    "akbla"
    "baaab"
    "bbbbb"
    "baaab"
    "abbba"
    "fbbbf"
    "abbga"
    "abbba"
    "abbba"
    "fbbbf"
    "abbba"
    "bafab"
    "bbbbb"
    "aaaaa"
    "aaaaa"
    "aaaaa"
    "aaaga"
    "aaaaa"
    "abhba"
    "aibja"
    "abmba"
    "baaab"
    "bbbbb"
    "baaab"
    "abbba"
    "abbba"
    "abbga"
    "baaab"
    "baaab"
    "baaab"
    "baaab"
    "bbbbb"
    "bbbbb"
    "bafab"
    "abbba"
    "fbbbf"
    "abbga"
    "bafab"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bafab"
    "abbba"
    "fbbbf"
    "abbga"
    "bafab"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "baaab"
    "abbba"
    "abbba"
    "abbga"
    "baaab"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "aaaaa"
    "aaaaa"
    "aaaaa"
    "aaaga"
    "aaaaa"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "baaab"
    "abbba"
    "abbba"
    "abbba"
    "baaab"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbabb"
    "bbbbb"
    "abbba"
    "bbbbb"
    "bbabb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
    "bbbbb"
;

// ---- library (8x9x9, 14 specs) ----
constexpr BlockSpec kSpecs3[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_PLANKS, 0, 0, 0},
    {SPEC_BOOKSHELF, 0, 0, 0},
    {SPEC_CRAFTING, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_WOOD_STAIRS, 1, 0, 0},
    {SPEC_WOOD_STAIRS, 2, 0, 0},
    {SPEC_COBBLE_STAIRS, 1, 0, 0},
    {SPEC_DOOR, 2, 0, 0},
    {SPEC_DOOR, 2, 1, 0},
    {SPEC_FENCE, 0, 0, 0},
    {SPEC_PLATE, 0, 0, 0},
    {SPEC_WOOD_STAIRS, 3, 0, 0},
};
constexpr char kVolume3[] =
    "eaaaaaae"
    "iaaaaaae"
    "eaaaaaae"
    "eaaaaaae"
    "eaaaaaae"
    "eaaaaaae"
    "eaaaaaae"
    "eaaaaaae"
    "eaaaaaae"
    "eaaaaaae"
    "ejeeeeae"
    "eaeeeeae"
    "eaeeegae"
    "eaeelgae"
    "eaeeegae"
    "eaeelgae"
    "eadenbae"
    "eaaaaaae"
    "eabffbae"
    "ekeeeebe"
    "ebeeeefe"
    "ebeeeefe"
    "efeemebe"
    "efeeeefe"
    "efeemefe"
    "ebeeeebe"
    "eabffbae"
    "eabffbae"
    "ebeeecbe"
    "ebeeecbe"
    "ebeeecbe"
    "efeeecbe"
    "efeeecbe"
    "efeeecbe"
    "ebeeecbe"
    "eabffbae"
    "eabbbbae"
    "ebbeebbe"
    "ebbeebbe"
    "ebbeebbe"
    "ebbeebbe"
    "ebbeebbe"
    "ebbeebbe"
    "ebbeebbe"
    "eabbbbae"
    "gaaaaaah"
    "gaaaaaah"
    "gaaaaaah"
    "gaaaaaah"
    "gaaaaaah"
    "gaaaaaah"
    "gaaaaaah"
    "gaaaaaah"
    "gaaaaaah"
    "egaaaahe"
    "egaaaahe"
    "egaaaahe"
    "egaaaahe"
    "egaaaahe"
    "egaaaahe"
    "egaaaahe"
    "egaaaahe"
    "egaaaahe"
    "eegaahee"
    "eegaahee"
    "eegaahee"
    "eegaahee"
    "eegaahee"
    "eegaahee"
    "eegaahee"
    "eegaahee"
    "eegaahee"
    "eeegheee"
    "eeegheee"
    "eeegheee"
    "eeegheee"
    "eeegheee"
    "eeegheee"
    "eeegheee"
    "eeegheee"
    "eeegheee"
;

// ---- streetlight (3x4x3, 7 specs) ----
constexpr BlockSpec kSpecs4[] = {
    {SPEC_WOOL, 0, 0, 15},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_WALL_TORCH, 4, 0, 0},
    {SPEC_FENCE, 0, 0, 0},
    {SPEC_WALL_TORCH, 2, 0, 0},
    {SPEC_WALL_TORCH, 1, 0, 0},
    {SPEC_WALL_TORCH, 3, 0, 0},
};
constexpr char kVolume4[] =
    "bbb"
    "bdb"
    "bbb"
    "bbb"
    "bdb"
    "bbb"
    "bbb"
    "bdb"
    "bbb"
    "bcb"
    "eaf"
    "bgb"
;

// ---- tier 1 farm (9x1x7, 5 specs) ----
constexpr BlockSpec kSpecs5[] = {
    {SPEC_LOG, 0, 0, 0},
    {SPEC_FARMLAND, 0, 7, 0},
    {SPEC_FARMLAND, 0, 0, 0},
    {SPEC_WATER, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
};
constexpr char kVolume5[] =
    "aaaaaaaaa"
    "abbbbbbba"
    "abbcbbbba"
    "addddddda"
    "abbbbbbba"
    "abbbbbbba"
    "aaaaaaaaa"
;

// ---- tier 1 house (6x5x4, 11 specs) ----
constexpr BlockSpec kSpecs6[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_LOG, 0, 0, 0},
    {SPEC_PLANKS, 0, 0, 0},
    {SPEC_DIRT, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_COBBLE_STAIRS, 2, 0, 0},
    {SPEC_DOOR, 1, 0, 0},
    {SPEC_DOOR, 1, 1, 0},
    {SPEC_FENCE, 0, 0, 0},
    {SPEC_PLATE, 0, 0, 0},
};
constexpr char kVolume6[] =
    "aaaaae"
    "adddag"
    "adddae"
    "aaaaae"
    "bcccbe"
    "ceeehe"
    "cjeece"
    "bcccbe"
    "bcfcbe"
    "ceeeie"
    "ckeece"
    "bcfcbe"
    "bcccbe"
    "ceeece"
    "ceeece"
    "bcccbe"
    "ebbbee"
    "bbbbbe"
    "bbbbbe"
    "ebbbee"
;

// ---- tier 2 farm (9x1x13, 4 specs) ----
constexpr BlockSpec kSpecs7[] = {
    {SPEC_LOG, 0, 0, 0},
    {SPEC_FARMLAND, 0, 7, 0},
    {SPEC_WATER, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
};
constexpr char kVolume7[] =
    "aaaaaaaaa"
    "abbbbbbba"
    "abbbbbbba"
    "accccccca"
    "abbbbbbba"
    "abbbbbbba"
    "aaaaaaaaa"
    "abbbbbbba"
    "abbbbbbba"
    "accccccca"
    "abbbbbbba"
    "abbbbbbba"
    "aaaaaaaaa"
;

// ---- tier 2 house (6x6x4, 11 specs) ----
constexpr BlockSpec kSpecs8[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_LOG, 0, 0, 0},
    {SPEC_PLANKS, 0, 0, 0},
    {SPEC_DIRT, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_COBBLE_STAIRS, 2, 0, 0},
    {SPEC_FENCE, 0, 0, 0},
    {SPEC_DOOR, 1, 0, 0},
    {SPEC_PLATE, 0, 0, 0},
    {SPEC_DOOR, 1, 1, 0},
};
constexpr char kVolume8[] =
    "aaaaae"
    "adddag"
    "adddae"
    "aaaaae"
    "bcccbe"
    "cheeie"
    "ceeece"
    "bcccbe"
    "bcfcbe"
    "cjeeke"
    "ceeece"
    "bcfcbe"
    "bcccbe"
    "ceeece"
    "ceeece"
    "bcccbe"
    "ebbbee"
    "beeebe"
    "beeebe"
    "ebbbee"
    "eeeeee"
    "ebbbee"
    "ebbbee"
    "eeeeee"
;

// ---- tier 3 house (5x5x6, 7 specs) ----
constexpr BlockSpec kSpecs9[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_PLANKS, 0, 0, 0},
    {SPEC_LOG, 0, 0, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_WALL_TORCH, 4, 0, 0},
    {SPEC_COBBLE_STAIRS, 4, 0, 0},
};
constexpr char kVolume9[] =
    "aaaaa"
    "aaaaa"
    "aaaaa"
    "aaaaa"
    "aaaaa"
    "eegee"
    "abbba"
    "beeeb"
    "beeeb"
    "beeeb"
    "abeba"
    "eeeee"
    "abdba"
    "beeeb"
    "deeed"
    "beeeb"
    "abeba"
    "eeeee"
    "abbba"
    "beeeb"
    "beeeb"
    "befeb"
    "abbba"
    "eeeee"
    "ccccc"
    "cbbbc"
    "cbbbc"
    "cbbbc"
    "ccccc"
    "eeeee"
;

// ---- tier 4 house (6x6x5, 9 specs) ----
constexpr BlockSpec kSpecs10[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_PLANKS, 0, 0, 0},
    {SPEC_LOG, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_FENCE, 0, 0, 0},
    {SPEC_COBBLE_STAIRS, 2, 0, 0},
    {SPEC_WALL_TORCH, 2, 0, 0},
    {SPEC_LADDER, 1, 0, 0},
};
constexpr char kVolume10[] =
    "aaaaad"
    "aaaaad"
    "aaaaag"
    "aaaaad"
    "aaaaad"
    "abbbad"
    "bdddbd"
    "bddddd"
    "biddbd"
    "abbbad"
    "abebad"
    "bdddbd"
    "eddddd"
    "biddbd"
    "abebad"
    "abbbad"
    "bdddbd"
    "bddhbd"
    "biddbd"
    "abbbad"
    "cccccd"
    "cbbbcd"
    "cbbbcd"
    "cibbcd"
    "cccccd"
    "fffffd"
    "fdddfd"
    "fdddfd"
    "fdddfd"
    "fffffd"
;

// ---- tier 5 house (12x7x9, 12 specs) ----
constexpr BlockSpec kSpecs11[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_LOG, 0, 0, 0},
    {SPEC_PLANKS, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_GLASS_PANE, 0, 0, 0},
    {SPEC_WOOD_STAIRS, 1, 0, 0},
    {SPEC_WOOD_STAIRS, 2, 0, 0},
    {SPEC_WOOD_STAIRS, 3, 0, 0},
    {SPEC_DOOR, 2, 0, 0},
    {SPEC_DOOR, 2, 1, 0},
    {SPEC_WALL_TORCH, 1, 0, 0},
    {SPEC_WOOD_STAIRS, 4, 0, 0},
};
constexpr char kVolume11[] =
    "daaaaaaddddd"
    "daccccaddddd"
    "faccccaaaaaa"
    "daccccccccca"
    "daccccccccca"
    "daccccccccca"
    "daccccccccca"
    "daccccccccca"
    "daaaaaaaaaaa"
    "daaaaaaddddd"
    "daddddaddddd"
    "diddddaaaaaa"
    "daddddddddda"
    "daddddddddda"
    "daddddddddda"
    "daddddddddda"
    "daddddddddda"
    "daaaaaaaaaaa"
    "dabeebaddddd"
    "dcddddcddddd"
    "djddddcbeeba"
    "dcddddddddda"
    "dbddddddddda"
    "deddddddddda"
    "dbddddddddda"
    "dcddddddddda"
    "dabeebcbeeba"
    "faaaaaagdddd"
    "fcddddcchhhh"
    "fckdddcaaaaa"
    "fcddddddddda"
    "fcddddddddda"
    "fcddddddddda"
    "fcddddddddda"
    "fcddddddddda"
    "faaaaaaaaaaa"
    "dfccccgddddd"
    "dfcddcgddddd"
    "dfcddcchhhhh"
    "dfcddccccccc"
    "dfcddddddddb"
    "dfcdddddddde"
    "dfcddddddddb"
    "dfcccccccccc"
    "dfccccllllll"
    "ddfccgdddddd"
    "ddfccgdddddd"
    "ddfccgdddddd"
    "ddfccchhhhhh"
    "ddfccccccccc"
    "ddfccddddddc"
    "ddfccccccccc"
    "ddfcccllllll"
    "ddfccgdddddd"
    "dddfgddddddd"
    "dddfgddddddd"
    "dddfgddddddd"
    "dddfgddddddd"
    "dddfchhhhhhh"
    "dddfcccccccc"
    "dddfclllllll"
    "dddfgddddddd"
    "dddfgddddddd"
;

// ---- well (6x5x6, 4 specs) ----
constexpr BlockSpec kSpecs12[] = {
    {SPEC_COBBLE, 0, 0, 0},
    {SPEC_AIR, 0, 0, 0},
    {SPEC_FENCE, 0, 0, 0},
    {SPEC_WATER, 0, 0, 0},
};
constexpr char kVolume12[] =
    "aaaaaa"
    "aaaaaa"
    "aaddaa"
    "aaddaa"
    "aaaaaa"
    "aaaaaa"
    "bbbbbb"
    "baaaab"
    "babbab"
    "babbab"
    "baaaab"
    "bbbbbb"
    "bbbbbb"
    "bcbbcb"
    "bbbbbb"
    "bbbbbb"
    "bcbbcb"
    "bbbbbb"
    "bbbbbb"
    "bcbbcb"
    "bbbbbb"
    "bbbbbb"
    "bcbbcb"
    "bbbbbb"
    "bbbbbb"
    "baaaab"
    "baaaab"
    "baaaab"
    "baaaab"
    "bbbbbb"
;

// One static instance per piece. The trailing field is the java
// compass the ENTRANCE faces outward (0 = no door in the export),
// which is what VillagePopulator rotates toward the road.
const VillagePiece kPiece0{"blacksmith", 8, 6, 10, kSpecs0, 16, kVolume0, 0};  // blacksmith, entrance none
const VillagePiece kPiece1{"butcher shop", 12, 7, 9, kSpecs1, 17, kVolume1, 1};  // butcher_shop, entrance east
const VillagePiece kPiece2{"church", 5, 12, 10, kSpecs2, 13, kVolume2, 4};  // church, entrance north
const VillagePiece kPiece3{"library", 8, 9, 9, kSpecs3, 14, kVolume3, 2};  // library, entrance west
const VillagePiece kPiece4{"streetlight", 3, 4, 3, kSpecs4, 7, kVolume4, 0};  // streetlight, entrance none
const VillagePiece kPiece5{"tier 1 farm", 9, 1, 7, kSpecs5, 5, kVolume5, 0};  // tier_1_farm, entrance none
const VillagePiece kPiece6{"tier 1 house", 6, 5, 4, kSpecs6, 11, kVolume6, 1};  // tier_1_house, entrance east
const VillagePiece kPiece7{"tier 2 farm", 9, 1, 13, kSpecs7, 4, kVolume7, 0};  // tier_2_farm, entrance none
const VillagePiece kPiece8{"tier 2 house", 6, 6, 4, kSpecs8, 11, kVolume8, 1};  // tier_2_house, entrance east
const VillagePiece kPiece9{"tier 3 house", 5, 5, 6, kSpecs9, 7, kVolume9, 0};  // tier_3_house, entrance none
const VillagePiece kPiece10{"tier 4 house", 6, 6, 5, kSpecs10, 9, kVolume10, 0};  // tier_4_house, entrance none
const VillagePiece kPiece11{"tier 5 house", 12, 7, 9, kSpecs11, 12, kVolume11, 2};  // tier_5_house, entrance west
const VillagePiece kPiece12{"well", 6, 5, 6, kSpecs12, 4, kVolume12, 0};  // well, entrance none

// Indexable piece table (matches the PieceKind enum order).
const Structure* kPieces[] = {
    &kPiece0,
    &kPiece1,
    &kPiece2,
    &kPiece3,
    &kPiece4,
    &kPiece5,
    &kPiece6,
    &kPiece7,
    &kPiece8,
    &kPiece9,
    &kPiece10,
    &kPiece11,
    &kPiece12,
};

} // namespace

const Structure* piece(PieceKind k) { return kPieces[k]; }

int pieceFrontFacing(PieceKind k) {
    return static_cast<const VillagePiece*>(kPieces[k])->frontFacing();
}

} // namespace cursestone::level::generator::structure::village
