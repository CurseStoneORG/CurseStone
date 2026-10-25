/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
// ItemConstants.h — item id constants.
//
// Mirrors cn.nukkit.item.item, which itself mirrors item/item.
// All Block IDs are also here (since
// MCPE 0.15 uses item IDs < 256 for block items).
//
// Usage:
//   #include "item/ItemConstants.h"
//   uint16_t id = cursestone::item::STAGE;
#pragma once
#include <cstdint>

namespace cursestone::item {

// ============================================================
// Block IDs (0-255) — these double as item IDs for block items
// ============================================================
constexpr int AIR = 0;
constexpr int STONE = 1;
constexpr int GRASS = 2;
constexpr int DIRT = 3;
constexpr int COBBLESTONE = 4;
constexpr int COBBLE = 4;
constexpr int PLANK = 5;
constexpr int PLANKS = 5;
constexpr int WOODEN_PLANK = 5;
constexpr int WOODEN_PLANKS = 5;
constexpr int SAPLING = 6;
constexpr int SAPLINGS = 6;
constexpr int BEDROCK = 7;
constexpr int WATER = 8;
constexpr int STILL_WATER = 9;
constexpr int LAVA = 10;
constexpr int STILL_LAVA = 11;
constexpr int SAND = 12;
constexpr int GRAVEL = 13;
constexpr int GOLD_ORE = 14;
constexpr int IRON_ORE = 15;
constexpr int COAL_ORE = 16;
constexpr int LOG = 17;
constexpr int WOOD = 17;
constexpr int TRUNK = 17;
constexpr int LEAVES = 18;
constexpr int LEAVE = 18;
constexpr int SPONGE = 19;
constexpr int GLASS = 20;
constexpr int LAPIS_ORE = 21;
constexpr int LAPIS_BLOCK = 22;
constexpr int DISPENSER = 23;
constexpr int SANDSTONE = 24;
constexpr int NOTEBLOCK = 25;
constexpr int BED_BLOCK = 26;
constexpr int POWERED_RAIL = 27;
constexpr int DETECTOR_RAIL = 28;
constexpr int STICKY_PISTON = 29;
constexpr int COBWEB = 30;
constexpr int TALL_GRASS = 31;
constexpr int BUSH = 32;
constexpr int DEAD_BUSH = 32;
constexpr int WOOL = 35;
constexpr int DANDELION = 37;
constexpr int POPPY = 38;
constexpr int ROSE = 38;
constexpr int FLOWER = 38;
constexpr int RED_FLOWER = 38;
constexpr int BROWN_MUSHROOM = 39;
constexpr int RED_MUSHROOM = 40;
constexpr int GOLD_BLOCK = 41;
constexpr int IRON_BLOCK = 42;
constexpr int DOUBLE_SLAB = 43;
constexpr int SLAB = 44;
constexpr int BRICKS = 45;
constexpr int BRICKS_BLOCK = 45;
constexpr int TNT = 46;
constexpr int BOOKSHELF = 47;
constexpr int MOSS_STONE = 48;
constexpr int MOSSY_STONE = 48;
constexpr int OBSIDIAN = 49;
constexpr int TORCH = 50;
constexpr int FIRE = 51;
constexpr int MONSTER_SPAWNER = 52;
constexpr int WOOD_STAIRS = 53;
constexpr int OAK_WOOD_STAIRS = 53;
constexpr int CHEST = 54;
constexpr int REDSTONE_WIRE = 55;
constexpr int DIAMOND_ORE = 56;
constexpr int DIAMOND_BLOCK = 57;
constexpr int CRAFTING_TABLE = 58;
constexpr int WORKBENCH = 58;
constexpr int WHEAT_BLOCK = 59;
constexpr int FARMLAND = 60;
constexpr int FURNACE = 61;
constexpr int BURNING_FURNACE = 62;
constexpr int SIGN_POST = 63;
constexpr int DOOR_BLOCK = 64;
constexpr int LADDER = 65;
constexpr int RAIL = 66;
constexpr int COBBLE_STAIRS = 67;
constexpr int COBBLESTONE_STAIRS = 67;
constexpr int WALL_SIGN = 68;
constexpr int LEVER = 69;
constexpr int STONE_PRESSURE_PLATE = 70;
constexpr int IRON_DOOR_BLOCK = 71;
constexpr int WOOD_PRESSURE_PLATE = 72;
constexpr int REDSTONE_ORE = 73;
constexpr int GLOWING_REDSTONE_ORE = 74;
constexpr int REDSTONE_TORCH = 75;
constexpr int REDSTONE_TORCH_ON = 75;
constexpr int STONE_BUTTON = 76;
constexpr int SNOW_LAYER = 78;
constexpr int ICE = 79;
constexpr int SNOW_BLOCK = 80;
constexpr int CLAY_BLOCK = 82;
constexpr int REEDS = 83;
constexpr int JUKEBOX = 84;
constexpr int FENCE = 85;
constexpr int PUMPKIN = 86;
constexpr int NETHERRACK = 87;
constexpr int SOUL_SAND = 88;
constexpr int GLOWSTONE = 89;
constexpr int NETHER_PORTAL = 90;
constexpr int JACK_O_LANTERN = 91;
constexpr int CAKE_BLOCK = 92;
constexpr int UNPOWERED_REPEATER = 93;
constexpr int POWERED_REPEATER = 94;
constexpr int STAINED_GLASS = 95;
constexpr int TRAPDOOR = 96;
constexpr int MONSTER_EGG = 97;
constexpr int STONE_BRICKS = 98;
constexpr int STONE_BRICK = 98;
constexpr int BROWN_MUSHROOM_BLOCK = 99;
constexpr int RED_MUSHROOM_BLOCK = 100;
constexpr int IRON_BARS = 101;
constexpr int GLASS_PANE = 102;
constexpr int MELON_BLOCK = 103;
constexpr int PUMPKIN_STEM = 104;
constexpr int MELON_STEM = 105;
constexpr int VINE = 106;
constexpr int FENCE_GATE = 107;
constexpr int BRICK_STAIRS = 108;
constexpr int STONE_BRICK_STAIRS = 109;
constexpr int MYCELIUM = 110;
constexpr int WATERLILY = 111;
constexpr int NETHER_BRICKS = 112;
constexpr int NETHER_BRICK_FENCE = 113;
constexpr int NETHER_BRICK_STAIRS = 114;
constexpr int ENCHANTING_TABLE = 116;
constexpr int ENCHANT_TABLE = 116;
constexpr int BREWING_STAND_BLOCK = 117;
constexpr int BREWING_STAND_BLOCK_ITEM = 117;
constexpr int CAULDRON_BLOCK = 118;
constexpr int CAULDRON_BLOCK_ITEM = 118;
constexpr int DRAGON_EGG = 122;
constexpr int REDSTONE_LAMP = 123;
constexpr int COCOA_BLOCK = 127;
constexpr int SANDSTONE_STAIRS = 128;
constexpr int EMERALD_ORE = 129;
constexpr int ENDER_CHEST = 130;
constexpr int TRIPWIRE_HOOK = 131;
constexpr int TRIPWIRE = 132;
constexpr int EMERALD_BLOCK = 133;
constexpr int SPRUCE_WOOD_STAIRS = 134;
constexpr int BIRCH_WOOD_STAIRS = 135;
constexpr int JUNGLE_WOOD_STAIRS = 136;
constexpr int COMMAND_BLOCK = 137;
constexpr int BEACON = 138;
constexpr int COBBLE_WALL = 139;
constexpr int STONE_WALL = 139;
constexpr int FLOWER_POT = 140;
constexpr int CARROT_BLOCK = 141;
constexpr int POTATO_BLOCK = 142;
constexpr int WOODEN_BUTTON = 143;
constexpr int SKULL_BLOCK = 144;
constexpr int ANVIL = 145;
constexpr int TRAPPED_CHEST = 146;
constexpr int WEIGHTED_PRESSURE_PLATE_LIGHT = 147;
constexpr int WEIGHTED_PRESSURE_PLATE_HEAVY = 148;
constexpr int DAYLIGHT_DETECTOR = 151;
constexpr int REDSTONE_BLOCK = 152;
constexpr int QUARTZ_BLOCK = 155;
constexpr int QUARTZ_STAIRS = 156;
constexpr int DOUBLE_WOOD_SLAB = 157;
constexpr int WOOD_SLAB = 158;
constexpr int ACTIVATOR_RAIL = 157;
constexpr int DROPPER = 158;
constexpr int STAINED_CLAY = 159;
constexpr int STAINED_HARDENED_CLAY = 159;
constexpr int STAINED_GLASS_PANE = 160;
constexpr int LEAVES2 = 161;
constexpr int LEAVE2 = 161;
constexpr int LOG2 = 162;
constexpr int WOOD2 = 162;
constexpr int TRUNK2 = 162;
constexpr int ACACIA_WOOD_STAIRS = 163;
constexpr int DARK_OAK_WOOD_STAIRS = 164;
constexpr int SLIME_BLOCK = 165;
constexpr int IRON_TRAPDOOR = 167;
constexpr int PRISMARINE = 168;
constexpr int SEA_LANTERN = 169;
constexpr int HAY_BALE = 170;
constexpr int CARPET = 171;
constexpr int HARDENED_CLAY = 172;
constexpr int COAL_BLOCK = 173;
constexpr int PACKED_ICE = 174;
constexpr int DOUBLE_PLANT = 175;
constexpr int RED_SANDSTONE = 179;
constexpr int RED_SANDSTONE_STAIRS = 180;
constexpr int DOUBLE_RED_SANDSTONE_SLAB = 181;
constexpr int RED_SANDSTONE_SLAB = 182;
constexpr int SPRUCE_FENCE_GATE = 183;
constexpr int BIRCH_FENCE_GATE = 184;
constexpr int JUNGLE_FENCE_GATE = 185;
constexpr int DARK_OAK_FENCE_GATE = 186;
constexpr int ACACIA_FENCE_GATE = 187;
constexpr int SPRUCE_FENCE = 188;
constexpr int BIRCH_FENCE = 189;
constexpr int JUNGLE_FENCE = 190;
constexpr int DARK_OAK_FENCE = 191;
constexpr int ACACIA_FENCE = 192;
constexpr int SPRUCE_DOOR_BLOCK = 193;
constexpr int BIRCH_DOOR_BLOCK = 194;
constexpr int JUNGLE_DOOR_BLOCK = 195;
constexpr int ACACIA_DOOR_BLOCK = 196;
constexpr int DARK_OAK_DOOR_BLOCK = 197;
constexpr int CHORUS_PLANT = 209;
constexpr int CHORUS_FLOWER = 210;
constexpr int PURPUR_BLOCK = 201;
constexpr int PURPUR_PILLAR = 202;
constexpr int PURPUR_STAIRS = 203;
constexpr int GRASS_PATH = 198;

// ============================================================
// item IDs (256+)
// ============================================================
constexpr int IRON_SHOVEL = 256;
constexpr int IRON_PICKAXE = 257;
constexpr int IRON_AXE = 258;
constexpr int FLINT_STEEL = 259;
constexpr int FLINT_AND_STEEL = 259;
constexpr int APPLE = 260;
constexpr int BOW = 261;
constexpr int ARROW = 262;
constexpr int COAL = 263;
constexpr int DIAMOND = 264;
constexpr int IRON_INGOT = 265;
constexpr int GOLD_INGOT = 266;
constexpr int IRON_SWORD = 267;
constexpr int WOODEN_SWORD = 268;
constexpr int WOOD_SWORD = 268;
constexpr int WOODEN_SHOVEL = 269;
constexpr int WOOD_SHOVEL = 269;
constexpr int WOODEN_PICKAXE = 270;
constexpr int WOOD_PICKAXE = 270;
constexpr int WOODEN_AXE = 271;
constexpr int WOOD_AXE = 271;
constexpr int STONE_SWORD = 272;
constexpr int STONE_SHOVEL = 273;
constexpr int STONE_PICKAXE = 274;
constexpr int STONE_AXE = 275;
constexpr int DIAMOND_SWORD = 276;
constexpr int DIAMOND_SHOVEL = 277;
constexpr int DIAMOND_PICKAXE = 278;
constexpr int DIAMOND_AXE = 279;
constexpr int STICK = 280;
constexpr int BOWL = 281;
constexpr int MUSHROOM_STEW = 282;
constexpr int GOLD_SWORD = 283;
constexpr int GOLD_SHOVEL = 284;
constexpr int GOLD_PICKAXE = 285;
constexpr int GOLD_AXE = 286;
constexpr int STRING = 287;
constexpr int FEATHER = 288;
constexpr int GUNPOWDER = 289;
constexpr int WOODEN_HOE = 290;
constexpr int WOOD_HOE = 290;
constexpr int STONE_HOE = 291;
constexpr int IRON_HOE = 292;
constexpr int DIAMOND_HOE = 293;
constexpr int GOLD_HOE = 294;
constexpr int SEEDS = 295;
constexpr int WHEAT_SEEDS = 295;
constexpr int WHEAT = 296;
constexpr int BREAD = 297;
constexpr int LEATHER_CAP = 298;
constexpr int LEATHER_HELMET = 298;
constexpr int LEATHER_TUNIC = 299;
constexpr int LEATHER_CHESTPLATE = 299;
constexpr int LEATHER_PANTS = 300;
constexpr int LEATHER_LEGGINGS = 300;
constexpr int LEATHER_BOOTS = 301;
constexpr int CHAIN_HELMET = 302;
constexpr int CHAIN_CHESTPLATE = 303;
constexpr int CHAIN_LEGGINGS = 304;
constexpr int CHAIN_BOOTS = 305;
constexpr int IRON_HELMET = 306;
constexpr int IRON_CHESTPLATE = 307;
constexpr int IRON_LEGGINGS = 308;
constexpr int IRON_BOOTS = 309;
constexpr int DIAMOND_HELMET = 310;
constexpr int DIAMOND_CHESTPLATE = 311;
constexpr int DIAMOND_LEGGINGS = 312;
constexpr int DIAMOND_BOOTS = 313;
constexpr int GOLD_HELMET = 314;
constexpr int GOLD_CHESTPLATE = 315;
constexpr int GOLD_LEGGINGS = 316;
constexpr int GOLD_BOOTS = 317;
constexpr int FLINT = 318;
constexpr int RAW_PORKCHOP = 319;
constexpr int COOKED_PORKCHOP = 320;
constexpr int PAINTING = 321;
constexpr int GOLDEN_APPLE = 322;
constexpr int ENCHANTED_GOLDEN_APPLE = 466;
constexpr int SIGN = 323;
constexpr int WOODEN_DOOR = 324;
constexpr int WOOD_DOOR = 324;
constexpr int BUCKET = 325;
constexpr int MINECART = 328;
constexpr int SADDLE = 329;
constexpr int IRON_DOOR = 330;
constexpr int REDSTONE = 331;
constexpr int SNOWBALL = 332;
constexpr int BOAT = 333;
constexpr int LEATHER = 334;
constexpr int KELP = 335;
constexpr int BRICK = 336;
constexpr int CLAY = 337;
constexpr int SUGAR_CANE = 338;
constexpr int PAPER = 339;
constexpr int BOOK = 340;
constexpr int SLIMEBALL = 341;
constexpr int CHEST_MINECART = 342;
constexpr int HOPPER_MINECART = 343;
constexpr int BEEF = 363;
constexpr int RAW_BEEF = 363;
constexpr int STEAK = 364;
constexpr int COOKED_BEEF = 364;
constexpr int RAW_CHICKEN = 365;
constexpr int COOKED_CHICKEN = 366;
constexpr int ROTTEN_FLESH = 367;
constexpr int ENDER_PEARL = 368;
constexpr int BLAZE_ROD = 369;
constexpr int GHAST_TEAR = 370;
constexpr int GOLD_NUGGET = 371;
constexpr int NETHER_WART = 372;
constexpr int POTION = 373;
constexpr int GLASS_BOTTLE = 374;
constexpr int BOTTLE = 374;
constexpr int SPIDER_EYE = 375;
constexpr int FERMENTED_SPIDER_EYE = 376;
constexpr int BLAZE_POWDER = 377;
constexpr int MAGMA_CREAM = 378;
constexpr int BREWING_STAND = 379;
constexpr int CAULDRON = 380;
constexpr int ENDER_EYE = 381;
constexpr int SPECKLED_MELON = 382;
constexpr int SPAWN_EGG = 383;
constexpr int EXPERIENCE_BOTTLE = 384;
constexpr int FIRE_CHARGE = 385;
constexpr int FIREBALL = 385;
constexpr int BOOK_AND_QUILL = 386;
constexpr int WRITTEN_BOOK = 387;
constexpr int EMERALD = 388;
constexpr int ITEM_FRAME = 389;
constexpr int FLOWER_POT_ITEM = 390;
constexpr int CARROT = 391;
constexpr int POTATO = 392;
constexpr int BAKED_POTATO = 393;
constexpr int POISONOUS_POTATO = 394;
constexpr int EMPTY_MAP = 395;
constexpr int GOLDEN_CARROT = 396;
constexpr int SKULL = 397;
constexpr int CARROT_ON_A_STICK = 398;
constexpr int CARROT_STICK = 398;
constexpr int NETHER_STAR = 399;
constexpr int PUMPKIN_PIE = 400;
constexpr int ENCHANTMENT_BOTTLE = 437;
constexpr int ENCHANTED_BOOK = 403;
constexpr int COMPARATOR = 404;
constexpr int NETHER_BRICK = 405;
constexpr int QUARTZ = 406;
constexpr int TNT_MINECART = 407;
constexpr int HOPPER = 408;
constexpr int RAW_RABBIT = 411;
constexpr int COOKED_RABBIT = 412;
constexpr int RABBIT_STEW = 413;
constexpr int RABBIT_FOOT = 414;
constexpr int RABBIT_HIDE = 415;
constexpr int HORSE_ARMOR_IRON = 417;
constexpr int HORSE_ARMOR_GOLD = 418;
constexpr int HORSE_ARMOR_DIAMOND = 419;
constexpr int LEAD = 420;
constexpr int NAME_TAG = 421;
constexpr int NAMETAG = 421;
constexpr int COMMAND_BLOCK_MINECART = 443;
constexpr int RAW_MUTTON = 423;
constexpr int COOKED_MUTTON = 424;
constexpr int RAW_FISH = 349;
constexpr int RAW_SALMON = 460;
constexpr int CLOWNFISH = 461;
constexpr int PUFFERFISH = 462;
constexpr int COOKED_FISH = 350;
constexpr int COOKED_SALMON = 463;
constexpr int DYE = 351;
constexpr int INK_SAC = 351;
constexpr int BONE = 352;
constexpr int SUGAR = 353;
constexpr int CAKE = 354;
constexpr int BED = 355;
constexpr int EYE_DROP = 378; // alias for magma cream
constexpr int COOKIE = 357;
constexpr int SHEARS = 359;
constexpr int MELON = 360;
constexpr int MELON_SLICE = 360;
constexpr int RAW_CHICKEN_2 = 365;
constexpr int PUMPKIN_SEEDS = 361;
constexpr int MELON_SEEDS = 362;
constexpr int BEETROOT_SOUP = 459;
constexpr int BEETROOT_SEEDS = 458;
constexpr int BEETROOT_SEED = 458;
constexpr int BEETROOT = 457;
constexpr int SPLASH_POTION = 438;
constexpr int CAMERA = 456;
constexpr int ELYTRA = 444;

} // namespace cursestone::item
