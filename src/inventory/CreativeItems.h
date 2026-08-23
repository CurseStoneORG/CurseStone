// SPDX-License-Identifier: LGPL-3.0-or-later
// CreativeItems.h — the static creative-inventory item list.
//
// Mirrors pocketmine/resources/creativeitems.json for MCPE 0.15.x
// (protocol 84). Each entry is {itemId, damage}; count is always 1
// for creative items.
//
// We use a condensed but representative list (≈100 entries) covering
// the most-used building blocks, decoration, tools, food, and redstone
// so the player has a useful creative inventory without pulling in
// hundreds of niche items. The list is static const — no per-player
// mutation. PMMP also supports per-player extra creative items
// (`personalCreativeItems`) which we don't need for the MVP.
#pragma once
#include "item/ItemStack.h"
#include <vector>
#include <cstdint>

namespace cenisyspe::inventory {

struct CreativeItemDef {
    uint16_t id;
    uint8_t  damage;
};

} // namespace cenisyspe::inventory

// Implementation ———————————————————————————————————————————————
namespace cenisyspe::inventory {

// One global table — populated on first access (lazy).
inline const std::vector<CreativeItemDef>& creativeItemTable() {
    static const std::vector<CreativeItemDef> table = {
        // ---- Building blocks ----
        {1, 0},    // Stone
        {1, 1},    // Granite
        {1, 2},    // Polished Granite
        {1, 3},    // Diorite
        {1, 4},    // Polished Diorite
        {1, 5},    // Andesite
        {1, 6},    // Polished Andesite
        {2, 0},    // Grass
        {3, 0},    // Dirt
        {4, 0},    // Cobblestone
        {5, 0},    // Oak Planks
        {5, 1},    // Spruce Planks
        {5, 2},    // Birch Planks
        {5, 3},    // Jungle Planks
        {5, 4},    // Acacia Planks
        {5, 5},    // Dark Oak Planks
        {12, 0},   // Sand
        {13, 0},   // Gravel
        {17, 0},   // Oak Log
        {17, 1},   // Spruce Log
        {17, 2},   // Birch Log
        {17, 3},   // Jungle Log
        {20, 0},   // Glass
        {24, 0},   // Sandstone
        {24, 1},   // Chiseled Sandstone
        {24, 2},   // Smooth Sandstone
        {35, 0},   // White Wool
        {35, 1},   // Orange Wool
        {35, 2},   // Magenta Wool
        {35, 3},   // Light Blue Wool
        {35, 4},   // Yellow Wool
        {35, 5},   // Lime Wool
        {35, 6},   // Pink Wool
        {35, 7},   // Gray Wool
        {35, 8},   // Light Gray Wool
        {35, 9},   // Cyan Wool
        {35, 10},  // Purple Wool
        {35, 11},  // Blue Wool
        {35, 12},  // Brown Wool
        {35, 13},  // Green Wool
        {35, 14},  // Red Wool
        {35, 15},  // Black Wool
        {41, 0},   // Gold Block
        {42, 0},   // Iron Block
        {45, 0},   // Bricks
        {46, 0},   // TNT
        {47, 0},   // Bookshelf
        {48, 0},   // Moss Stone
        {49, 0},   // Obsidian
        {57, 0},   // Diamond Block
        {58, 0},   // Crafting Table
        {61, 0},   // Furnace
        {79, 0},   // Ice
        {80, 0},   // Snow Block
        {89, 0},   // Glowstone
        {95, 0},   // White Stained Glass
        {95, 1},   // Orange Stained Glass
        {95, 2},   // Magenta Stained Glass
        {95, 3},   // Light Blue Stained Glass
        {95, 4},   // Yellow Stained Glass
        {95, 5},   // Lime Stained Glass
        {95, 6},   // Pink Stained Glass
        {95, 7},   // Gray Stained Glass
        {95, 8},   // Light Gray Stained Glass
        {95, 9},   // Cyan Stained Glass
        {95, 10},  // Purple Stained Glass
        {95, 11},  // Blue Stained Glass
        {95, 12},  // Brown Stained Glass
        {95, 13},  // Green Stained Glass
        {95, 14},  // Red Stained Glass
        {95, 15},  // Black Stained Glass
        {98, 0},   // Stone Bricks
        {98, 1},   // Mossy Stone Bricks
        {98, 2},   // Cracked Stone Bricks
        {98, 3},   // Chiseled Stone Bricks
        // ---- Decoration ----
        {6, 0},    // Oak Sapling
        {6, 1},    // Spruce Sapling
        {6, 2},    // Birch Sapling
        {6, 3},    // Jungle Sapling
        {37, 0},   // Dandelion
        {38, 0},   // Poppy
        {38, 1},   // Blue Orchid
        {38, 2},   // Allium
        {38, 3},   // Azure Bluet
        {50, 0},   // Torch
        {53, 0},   // Oak Wood Stairs
        {65, 0},   // Ladder
        {89, 0},   // Glowstone (dup OK)
        // ---- Tools / weapons ----
        {256, 0},  // Iron Shovel
        {257, 0},  // Iron Pickaxe
        {258, 0},  // Iron Axe
        {267, 0},  // Iron Sword
        {268, 0},  // Wooden Sword
        {269, 0},  // Wooden Shovel
        {270, 0},  // Wooden Pickaxe
        {271, 0},  // Wooden Axe
        {272, 0},  // Stone Sword
        {273, 0},  // Stone Shovel
        {274, 0},  // Stone Pickaxe
        {275, 0},  // Stone Axe
        {276, 0},  // Diamond Sword
        {277, 0},  // Diamond Shovel
        {278, 0},  // Diamond Pickaxe
        {279, 0},  // Diamond Axe
        {283, 0},  // Gold Sword
        {284, 0},  // Gold Shovel
        {285, 0},  // Gold Pickaxe
        {286, 0},  // Gold Axe
        {290, 0},  // Wooden Hoe
        {291, 0},  // Stone Hoe
        {292, 0},  // Iron Hoe
        {293, 0},  // Diamond Hoe
        {294, 0},  // Gold Hoe
        {259, 0},  // Flint and Steel
        {346, 0},  // Fishing Rod
        {359, 0},  // Shears
        // ---- Armor ----
        {298, 0},  // Leather Cap
        {299, 0},  // Leather Tunic
        {300, 0},  // Leather Pants
        {301, 0},  // Leather Boots
        {302, 0},  // Chain Helmet
        {303, 0},  // Chain Chestplate
        {304, 0},  // Chain Leggings
        {305, 0},  // Chain Boots
        {306, 0},  // Iron Helmet
        {307, 0},  // Iron Chestplate
        {308, 0},  // Iron Leggings
        {309, 0},  // Iron Boots
        {310, 0},  // Diamond Helmet
        {311, 0},  // Diamond Chestplate
        {312, 0},  // Diamond Leggings
        {313, 0},  // Diamond Boots
        {314, 0},  // Gold Helmet
        {315, 0},  // Gold Chestplate
        {316, 0},  // Gold Leggings
        {317, 0},  // Gold Boots
        // ---- Misc ----
        {263, 0},  // Coal
        {264, 0},  // Diamond
        {265, 0},  // Iron Ingot
        {266, 0},  // Gold Ingot
        {265, 0},  // Iron Ingot
        {280, 0},  // Stick
        {287, 0},  // String
        {288, 0},  // Feather
        {289, 0},  // Gunpowder
        {295, 0},  // Wheat Seeds
        {296, 0},  // Wheat
        {297, 0},  // Bread
        {322, 0},  // Apple
        {336, 0},  // Brick
        {337, 0},  // Clay
        {338, 0},  // Sugar Cane
        {339, 0},  // Paper
        {340, 0},  // Book
        {345, 0},  // Clock
        {347, 0},  // Bed
        {348, 0},  // Cookie
        {351, 0},  // Ink Sack (dye)
        {351, 1},  // Rose Red
        {351, 2},  // Cactus Green
        {351, 3},  // Cocoa Beans
        {351, 4},  // Lapis Lazuli
        {351, 5},  // Purple Dye
        {351, 6},  // Cyan Dye
        {351, 7},  // Light Gray Dye
        {351, 8},  // Gray Dye
        {351, 9},  // Pink Dye
        {351, 10}, // Lime Dye
        {351, 11}, // Dandelion Yellow
        {351, 12}, // Light Blue Dye
        {351, 13}, // Magenta Dye
        {351, 14}, // Orange Dye
        {351, 15}, // Bone Meal
        // ---- Redstone ----
        {331, 0},  // Redstone
        {356, 0},  // Piston
        {354, 0},  // Cake
    };
    return table;
}

inline std::vector<cenisyspe::item::ItemStack> getCreativeItems() {
    const auto& table = creativeItemTable();
    std::vector<cenisyspe::item::ItemStack> out;
    out.reserve(table.size());
    for (const auto& e : table) {
        cenisyspe::item::ItemStack s;
        s.id    = e.id;
        s.meta  = e.damage;
        s.count = 1;
        out.push_back(s);
    }
    return out;
}

} // namespace cenisyspe::inventory
