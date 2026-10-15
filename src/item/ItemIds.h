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
// ItemIds — item IDs used in inventory slots and MobEquipmentPacket.
// MCPE 0.15.x item IDs share a namespace with block IDs for the
// first 255 (so item 1 == block 1 == Stone). Items with id > 255
// are pure items (tools, food, etc.).
#pragma once
#include <cstdint>
namespace cursestone::item {
enum ItemId : uint16_t {
    // Tools
    ITEM_WOOD_PICKAXE = 270,
    ITEM_STONE_PICKAXE = 274,
    ITEM_IRON_PICKAXE = 257,
    ITEM_GOLD_PICKAXE = 285,
    ITEM_DIAMOND_PICKAXE = 278,
    ITEM_WOOD_AXE = 271,
    ITEM_STONE_AXE = 275,
    ITEM_IRON_AXE = 258,
    ITEM_GOLD_AXE = 286,
    ITEM_DIAMOND_AXE = 279,
    ITEM_WOOD_SHOVEL = 269,
    ITEM_STONE_SHOVEL = 273,
    ITEM_IRON_SHOVEL = 256,
    ITEM_GOLD_SHOVEL = 284,
    ITEM_DIAMOND_SHOVEL = 277,
    ITEM_WOOD_SWORD = 268,
    ITEM_STONE_SWORD = 272,
    ITEM_IRON_SWORD = 267,
    ITEM_GOLD_SWORD = 283,
    ITEM_DIAMOND_SWORD = 276,
    ITEM_BOW = 261,
    ITEM_SHEARS = 359,
    ITEM_FLINT_STEEL = 259,
    ITEM_WOOD_HOE = 290,
    ITEM_STONE_HOE = 291,
    ITEM_IRON_HOE = 292,
    ITEM_GOLD_HOE = 294,
    ITEM_DIAMOND_HOE = 293,

    // Armor pieces
    ITEM_IRON_HELMET = 306,
    ITEM_IRON_CHESTPLATE = 307,
    ITEM_IRON_LEGGINGS = 308,
    ITEM_IRON_BOOTS = 309,
    ITEM_DIAMOND_HELMET = 310,
    ITEM_DIAMOND_CHESTPLATE = 311,
    ITEM_DIAMOND_LEGGINGS = 312,
    ITEM_DIAMOND_BOOTS = 313,
    ITEM_GOLD_HELMET = 314,
    ITEM_GOLD_CHESTPLATE = 315,
    ITEM_GOLD_LEGGINGS = 316,
    ITEM_GOLD_BOOTS = 317,
    ITEM_LEATHER_CAP = 298,
    ITEM_LEATHER_TUNIC = 299,
    ITEM_LEATHER_PANTS = 300,
    ITEM_LEATHER_BOOTS = 301,
    ITEM_CHAIN_HELMET = 302,
    ITEM_CHAIN_CHESTPLATE = 303,
    ITEM_CHAIN_LEGGINGS = 304,
    ITEM_CHAIN_BOOTS = 305,

    // Materials
    ITEM_STICK = 280,
    ITEM_COAL = 263,
    ITEM_DIAMOND = 264,
    ITEM_IRON_INGOT = 265,
    ITEM_GOLD_INGOT = 266,
    ITEM_EMERALD = 388,
    ITEM_REDSTONE = 331,
    ITEM_QUARTZ = 406,
    ITEM_FLINT = 318,
    ITEM_FEATHER = 288,
    ITEM_LEATHER = 334,
    ITEM_STRING = 287,
    ITEM_GUNPOWDER = 289,
    ITEM_NETHER_STAR = 399,
    ITEM_NETHER_QUARTZ = 406,

    // Foods
    ITEM_APPLE = 260,
    ITEM_GOLDEN_APPLE = 322,
    ITEM_RAW_BEEF = 363,
    ITEM_STEAK = 364,
    ITEM_RAW_PORKCHOP = 319,
    ITEM_COOKED_PORKCHOP = 320,
    ITEM_RAW_CHICKEN = 366,
    ITEM_COOKED_CHICKEN = 366,
    ITEM_BREAD = 297,
    ITEM_CARROT = 391,
    ITEM_POTATO = 392,
    ITEM_MELON_SLICE = 360,
    ITEM_COOKIE = 357,
    ITEM_EGG = 344,
    ITEM_SUGAR = 353,
    ITEM_WHEAT = 296,
    ITEM_SUGARCANE = 338,

    // Throwables
    ITEM_ARROW = 262,
    ITEM_SNOWBALL = 332,
    ITEM_EGG_2 = 344,
    ITEM_ENDER_PEARL = 368,

    // Containers
    ITEM_BUCKET = 325,
    ITEM_WATER_BUCKET = 326,
    ITEM_LAVA_BUCKET = 327,
    ITEM_MILK_BUCKET = 335,

    // Misc
    ITEM_SUGAR_CANE = 338,
    ITEM_SIGN = 323,
    ITEM_REDSTONE_DUST = 331,
    ITEM_BED = 355,
    ITEM_POTIONS = 437,
    ITEM_ENDER_EYE = 381,
    ITEM_BOOK = 340,
    ITEM_WHEAT_SEEDS = 295,
    ITEM_PUMPKIN_SEEDS = 361,
    ITEM_MELON_SEEDS = 362,
    ITEM_MAP = 358,
};
} // namespace
