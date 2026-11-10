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
// BlockMining.cpp — the vanilla mining rule table and the two queries the
// break path needs (canHarvest for drops, breakSeconds for the
// server-side validation of the client's own break progress).
#include "BlockMining.h"

#include "item/Item.h"
#include "item/ItemStack.h"
#include "item/ItemTool.h"

namespace cursestone::block {
namespace {

using item::ToolMaterial;
using item::ToolType;

// Vanilla tool speeds by material (only when the tool class matches).
constexpr float kMaterialSpeed[5] = {
    2.0f,  // TOOL_WOOD
    12.0f, // TOOL_GOLD
    4.0f,  // TOOL_STONE
    6.0f,  // TOOL_IRON
    8.0f,  // TOOL_DIAMOND
};

// Vanilla harvest level by material: the tier the tool reaches for
// tool-gated drops (gold mines at wood level, as in vanilla).
constexpr int kMaterialTier[5] = {
    0, // TOOL_WOOD
    0, // TOOL_GOLD
    1, // TOOL_STONE
    2, // TOOL_IRON
    3, // TOOL_DIAMOND
};

// Hand (no tool) — fails every tool-gated rule.
constexpr ToolType kNoTool = static_cast<ToolType>(-1);

// The vanilla rule for one block id. Entries follow the Java 1.9-era
// values the 0.15 client mirrors; unlisted ids hit the generic
// hand-harvestable fallback at the end (matches the old behavior for
// blocks this server does not stage).
struct Rule {
    float hardness;
    int8_t tool;
    int8_t tier; // -1 = not tool-gated (drops by hand).
};

constexpr Rule ruleFor(uint8_t id) {
    switch (id) {
    case 0:  return {0.0f, T_NONE, -1};   // air
    case 1:  return {1.5f, item::PICKAXE, 0};   // stone
    case 2:  return {0.6f, item::SHOVEL, -1};   // grass
    case 3:  return {0.5f, item::SHOVEL, -1};   // dirt
    case 4:  return {2.0f, item::PICKAXE, 0};   // cobblestone
    case 5:  return {2.0f, item::AXE, -1};      // planks
    case 6:  return {0.0f, T_NONE, -1};   // sapling
    case 7:  return {-1.0f, T_NONE, -1};  // bedrock
    case 8:  case 9:  case 10: case 11:
             return {-1.0f, T_NONE, -1};  // liquids: never timed
    case 12: return {0.5f, item::SHOVEL, -1};   // sand
    case 13: return {0.6f, item::SHOVEL, -1};   // gravel
    case 14: return {3.0f, item::PICKAXE, 2};   // gold ore (iron pick)
    case 15: return {3.0f, item::PICKAXE, 1};   // iron ore (stone pick)
    case 16: return {3.0f, item::PICKAXE, 0};   // coal ore
    case 17: return {2.0f, item::AXE, -1};      // log
    case 18: case 161:
             return {0.2f, T_NONE, -1};   // leaves (chance drops)
    case 19: return {0.6f, T_NONE, -1};   // sponge
    case 20: return {0.3f, T_NONE, -1};   // glass
    case 21: return {3.0f, item::PICKAXE, 1};   // lapis ore
    case 22: return {3.0f, item::PICKAXE, 1};   // lapis block
    case 23: return {3.5f, item::PICKAXE, 0};   // dispenser
    case 24: return {0.8f, item::PICKAXE, 0};   // sandstone
    case 25: return {0.8f, item::AXE, -1};      // note block
    case 26: return {0.2f, T_NONE, -1};   // bed
    case 27: case 28:
             return {0.7f, item::PICKAXE, 0};   // powered/detector rail
    case 29: case 33:
             return {1.5f, item::PICKAXE, 0};   // pistons
    case 30: return {4.0f, T_NONE, -1};   // cobweb (shears special-case)
    case 31: case 32:
             return {0.0f, T_NONE, -1};   // tall grass, dead bush
    case 34: return {1.5f, T_NONE, -1};   // piston head
    case 35: return {0.8f, T_NONE, -1};   // wool
    case 41: return {5.0f, item::PICKAXE, 1};   // iron block
    case 42: return {3.0f, item::PICKAXE, 2};   // gold block
    case 43: case 44:
             return {2.0f, item::PICKAXE, 0};   // (double) slab
    case 45: return {2.0f, item::PICKAXE, 0};   // bricks
    case 46: return {0.0f, T_NONE, -1};   // tnt
    case 47: return {1.5f, item::AXE, -1};      // bookshelf
    case 48: return {2.0f, item::PICKAXE, 0};   // mossy cobblestone
    case 49: return {50.0f, item::PICKAXE, 3};  // obsidian (diamond pick)
    case 50: case 51:
             return {0.0f, T_NONE, -1};   // torch, fire
    case 52: return {5.0f, item::PICKAXE, 0};   // mob spawner
    case 53: case 134: case 135: case 136: case 163: case 164:
             return {2.0f, item::AXE, -1};      // wooden stairs
    case 54: return {2.5f, item::AXE, -1};      // chest
    case 55: return {0.0f, T_NONE, -1};   // redstone wire
    case 56: return {3.0f, item::PICKAXE, 2};   // diamond ore (iron pick)
    case 57: return {5.0f, item::PICKAXE, 2};   // diamond block
    case 58: return {2.5f, item::AXE, -1};      // crafting table
    case 59: return {0.0f, T_NONE, -1};   // crops
    case 60: return {0.6f, item::SHOVEL, -1};   // farmland
    case 61: case 62:
             return {3.5f, item::PICKAXE, 0};   // (burning) furnace
    case 63: case 68:
             return {1.0f, item::AXE, -1};      // signs
    case 64: return {3.0f, item::AXE, -1};      // wooden door
    case 65: return {0.4f, item::AXE, -1};      // ladder
    case 66: return {0.7f, item::PICKAXE, 0};   // rail
    case 67: case 108: case 109: case 114: case 128: case 156:
             return {2.0f, item::PICKAXE, 0};   // stone-family stairs
    case 71: return {5.0f, item::PICKAXE, 0};   // iron door
    case 73: case 74:
             return {3.0f, item::PICKAXE, 2};   // redstone ore
    case 75: case 76:
             return {0.0f, T_NONE, -1};   // redstone torches
    case 78: return {0.1f, item::SHOVEL, -1};   // snow layer
    case 79: return {0.5f, item::PICKAXE, 0};   // ice
    case 80: return {0.2f, item::SHOVEL, -1};   // snow block
    case 81: return {0.4f, T_NONE, -1};   // cactus
    case 82: return {0.6f, item::SHOVEL, -1};   // clay
    case 83: return {0.0f, T_NONE, -1};   // sugar cane
    case 84: return {2.0f, item::AXE, -1};      // jukebox
    case 85: case 107:
             return {2.0f, item::AXE, -1};      // fence, fence gate
    case 86: case 91:
             return {1.0f, item::AXE, -1};      // pumpkin, jack o'lantern
    case 87: return {0.4f, item::PICKAXE, 0};   // netherrack
    case 88: return {0.5f, item::SHOVEL, -1};   // soul sand
    case 89: return {0.3f, T_NONE, -1};   // glowstone
    case 90: case 119: case 120:
             return {-1.0f, T_NONE, -1};  // portals
    case 92: return {0.5f, T_NONE, -1};   // cake
    case 93: case 94: case 143:
             return {0.0f, T_NONE, -1};   // repeater, comparator
    case 95: return {2.5f, item::AXE, -1};      // locked chest
    case 96: return {3.0f, item::AXE, -1};      // trapdoor
    case 98: return {1.5f, item::PICKAXE, 0};   // stone bricks
    case 99: case 100:
             return {0.2f, item::AXE, -1};      // mushroom blocks
    case 101: return {5.0f, item::PICKAXE, 0};  // iron bars
    case 102: case 160:
             return {0.3f, T_NONE, -1};   // glass panes
    case 103: return {1.0f, item::AXE, -1};     // melon
    case 104: case 105:
             return {0.0f, T_NONE, -1};   // stems
    case 106: return {0.2f, T_NONE, -1};  // vines
    case 110: return {0.6f, item::SHOVEL, -1};  // mycelium
    case 111: return {0.0f, T_NONE, -1};  // lily pad
    case 112: case 113:
             return {2.0f, item::PICKAXE, 0};   // nether brick, fence
    case 115: return {0.0f, T_NONE, -1};  // nether wart
    case 116: return {5.0f, item::PICKAXE, 0};  // enchanting table
    case 117: return {0.5f, item::PICKAXE, 0};  // brewing stand
    case 118: return {2.0f, item::PICKAXE, 0};  // cauldron
    case 121: return {3.0f, item::PICKAXE, 0};  // end stone
    case 123: case 124:
             return {0.3f, T_NONE, -1};   // redstone lamps
    case 125: case 154:
             return {3.0f, item::PICKAXE, 0};   // dropper, hopper
    case 129: return {3.0f, item::PICKAXE, 2};  // emerald ore
    case 130: return {22.5f, item::PICKAXE, 0}; // ender chest
    case 133: return {5.0f, item::PICKAXE, 2};  // emerald block
    case 139: return {2.0f, item::PICKAXE, 0};  // cobblestone wall
    case 141: case 142:
             return {0.0f, T_NONE, -1};   // carrots, potatoes
    case 145: return {5.0f, item::PICKAXE, 0};  // anvil
    case 152: return {5.0f, item::PICKAXE, 0};  // redstone block
    case 153: return {3.0f, item::PICKAXE, 0};  // quartz ore
    case 155: return {0.8f, item::PICKAXE, 0};  // quartz block
    case 158: return {0.5f, item::PICKAXE, 0};  // activator rail
    case 159: case 172:
             return {1.25f, item::PICKAXE, 0};  // (stained) hardened clay
    case 162: return {2.0f, item::AXE, -1};     // log2
    case 168: return {1.5f, item::PICKAXE, 0};  // prismarine
    case 169: return {0.3f, T_NONE, -1};  // sea lantern
    case 170: return {0.5f, T_NONE, -1};  // hay bale
    case 171: return {0.1f, T_NONE, -1};  // carpet
    case 173: return {5.0f, item::PICKAXE, 0};  // coal block
    case 174: return {0.5f, item::PICKAXE, 0};  // packed ice
    default: return {1.5f, T_NONE, -1};   // generic hand-harvestable block
    }
}

// The active rule for a block id, with the tool/tier resolved against the
// held stack. Returned by value: one switch, no globals, no locking.
struct ResolvedRule {
    float hardness;
    bool classMatch;  // held tool is the right class (or block has none)
    float speed;      // tool speed multiplier (1 for the hand)
    bool harvest;     // the drop may come out
};

ResolvedRule resolve(uint8_t blockId, const item::ItemStack& held) {
    const Rule r = ruleFor(blockId);
    ResolvedRule out;
    out.hardness = r.hardness;
    out.speed = 1.0f;
    out.classMatch = r.tool == T_NONE;
    out.harvest = r.tier < 0; // ungated blocks drop for every tool

    ToolType type = kNoTool;
    ToolMaterial material = ToolMaterial::TOOL_WOOD;
    if (const item::Item* it = item::Item::get(held.id)) {
        if (it->isTool()) {
            if (auto* tool = dynamic_cast<const item::ItemTool*>(it)) {
                type = tool->type();
                material = tool->material();
            }
        }
    }
    if (type != kNoTool) {
        if (r.tool == static_cast<int8_t>(type)) {
            out.classMatch = true;
            out.speed = kMaterialSpeed[static_cast<int>(material)];
        }
        if (r.tier >= 0 && kMaterialTier[static_cast<int>(material)] >= r.tier)
            out.harvest = true;
    }
    return out;
}

} // namespace

bool canHarvest(uint8_t blockId, const item::ItemStack& held) {
    return resolve(blockId, held).harvest;
}

float breakSeconds(uint8_t blockId, const item::ItemStack& held) {
    const ResolvedRule r = resolve(blockId, held);
    if (r.hardness < 0.0f)
        return -1.0f;
    if (r.hardness == 0.0f)
        return 0.0f;
    return r.hardness * (r.harvest ? 1.5f : 5.0f) / r.speed;
}

} // namespace cursestone::block
