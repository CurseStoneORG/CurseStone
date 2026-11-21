/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// BlockRegistry — registers every concrete Block subclass into the
// Block::get() flyweight map. Called once at server startup (phase 21).
#include "Block.h"
#include "BlockAir.h"
#include "BlockAnvil.h"
#include "BlockBedrock.h"
#include "BlockChest.h"
#include "BlockCoalOre.h"
#include "BlockCobblestone.h"
#include "BlockDiamond.h"
#include "BlockDiamondOre.h"
#include "BlockDirt.h"
#include "BlockEmerald.h"
#include "BlockEmeraldOre.h"
#include "BlockFallable.h"
#include "BlockFarmland.h"
#include "BlockFire.h"
#include "BlockFurnace.h"
#include "BlockGlass.h"
#include "BlockGlassPane.h"
#include "BlockGold.h"
#include "BlockGoldOre.h"
#include "BlockGrass.h"
#include "BlockGravel.h"
#include "BlockIce.h"
#include "BlockIron.h"
#include "BlockIronOre.h"
#include "BlockLapisOre.h"
#include "BlockLava.h"
#include "BlockLavaFlowing.h"
#include "BlockLeaves.h"
#include "BlockLog.h"
#include "BlockMagmaBlock.h"
#include "BlockNetherrack.h"
#include "BlockObsidian.h"
#include "BlockPlanks.h"
#include "BlockPortal.h"
#include "BlockProfiles.h"
#include "BlockRedstoneOre.h"
#include "BlockSapling.h"
#include "BlockSand.h"
#include "BlockSandstone.h"
#include "BlockSign.h"
#include "BlockSlab.h"
#include "BlockSnow.h"
#include "BlockSnowLayer.h"
#include "BlockSoulSand.h"
#include "BlockStone.h"
#include "BlockStoneBricks.h"
#include "BlockTNT.h"
#include "BlockTorch.h"
#include "BlockWater.h"
#include "BlockWaterFlowing.h"
#include "BlockWheat.h"
#include "BlockWood.h"
#include "BlockWoodStairs.h"
#include "BlockGlowstone.h"
#include "BlockIds.h"
#include <memory>
namespace cursestone::block {
namespace {
template <typename T>
std::unique_ptr<T> make_id() {
    return std::make_unique<T>();
}
}

void register_all_blocks() {
    Block::registerBlock(std::make_unique<BlockAir>());
    Block::registerBlock(std::make_unique<BlockBedrock>());
    Block::registerBlock(std::make_unique<BlockStone>());
    Block::registerBlock(std::make_unique<BlockDirt>());
    Block::registerBlock(std::make_unique<BlockGrass>());
    Block::registerBlock(std::make_unique<BlockCobblestone>());
    Block::registerBlock(std::make_unique<BlockPlanks>());
    Block::registerBlock(std::make_unique<BlockLog>());
    Block::registerBlock(std::make_unique<BlockLeaves>());
    Block::registerBlock(std::make_unique<BlockSand>());
    Block::registerBlock(std::make_unique<BlockSandstone>());
    Block::registerBlock(std::make_unique<BlockGravel>());
    Block::registerBlock(std::make_unique<BlockGlass>());
    Block::registerBlock(std::make_unique<BlockGlassPane>());
    Block::registerBlock(std::make_unique<BlockIce>());
    Block::registerBlock(std::make_unique<BlockSnow>());
    // Snow layer (78) — without a registered flyweight every Block::get(78)
    // came back null: the layer read as a full opaque block to the light
    // pass, refused placements into itself, and the vanilla replaceable-
    // target rule could never fire for it.
    Block::registerBlock(std::make_unique<BlockSnowLayer>());
    Block::registerBlock(std::make_unique<BlockGlowstone>());
    Block::registerBlock(std::make_unique<BlockNetherrack>());
    Block::registerBlock(std::make_unique<BlockSoulSand>());
    Block::registerBlock(std::make_unique<BlockObsidian>());
    Block::registerBlock(std::make_unique<BlockPortal>());
    Block::registerBlock(std::make_unique<BlockDiamond>());
    Block::registerBlock(std::make_unique<BlockDiamondOre>());
    Block::registerBlock(std::make_unique<BlockGold>());
    Block::registerBlock(std::make_unique<BlockGoldOre>());
    Block::registerBlock(std::make_unique<BlockIron>());
    Block::registerBlock(std::make_unique<BlockIronOre>());
    Block::registerBlock(std::make_unique<BlockLapisOre>());
    Block::registerBlock(std::make_unique<BlockRedstoneOre>());
    Block::registerBlock(std::make_unique<BlockEmerald>());
    Block::registerBlock(std::make_unique<BlockTNT>());
    Block::registerBlock(std::make_unique<BlockTorch>());
    Block::registerBlock(std::make_unique<BlockFire>());
    Block::registerBlock(std::make_unique<BlockFarmland>());
    Block::registerBlock(std::make_unique<BlockSapling>());
    Block::registerBlock(std::make_unique<BlockWheat>());
    Block::registerBlock(std::make_unique<BlockSign>());
    Block::registerBlock(std::make_unique<BlockSlab>());
    Block::registerBlock(std::make_unique<BlockChest>());
    Block::registerBlock(std::make_unique<BlockFurnace>());
    Block::registerBlock(std::make_unique<BlockLava>());
    Block::registerBlock(std::make_unique<BlockLavaFlowing>());
    Block::registerBlock(std::make_unique<BlockWater>());
    Block::registerBlock(std::make_unique<BlockWaterFlowing>());
    Block::registerBlock(std::make_unique<BlockWood>());
    Block::registerBlock(std::make_unique<BlockWoodStairs>());
    Block::registerBlock(std::make_unique<BlockStoneBricks>());
    Block::registerBlock(std::make_unique<BlockMagmaBlock>());
    // Anvil (145) — existed in MCPE 0.15.10 (BlockIds.h carried the id
    // all along); it was simply never registered, which left Block::get
    // (145) == nullptr (crash risk on support checks that do not null-
    // test, e.g. the bed-support check) and, with the falling-block
    // physics, an unregistered anvil could not fall. Registered LAST so
    // it does not disturb the ids above if something else re-registers.
    Block::registerBlock(std::make_unique<BlockAnvil>());

    // Coal ore (16) — the flyweight class existed all along but never
    // made it into this list, so every coal vein underground handed
    // Block::get() a null for the bare-deref call sites (join settle,
    // bed support, egg ground scan).
    Block::registerBlock(std::make_unique<BlockCoalOre>());

    // Palette ids real worlds carry without a bespoke class (see
    // BlockProfiles.h). Ground cover must not read as solid — the
    // reported join crash was a saved join spot inside tall grass (31)
    // dereferencing the null flyweight in the join settle — and thin
    // decor must not eat skylight, so both groups register with their
    // real profile instead of the generic fallback's solid shape. The
    // plain material blocks get their exact id and name on top of
    // fallback-equal behavior.
    Block::registerBlock(std::make_unique<BlockPlantLike>(TALL_GRASS_ID, "Tall Grass"));
    Block::registerBlock(std::make_unique<BlockPlantLike>(DEAD_BUSH, "Dead Bush"));
    Block::registerBlock(std::make_unique<BlockPlantLike>(DANDELION_ID, "Dandelion"));
    Block::registerBlock(std::make_unique<BlockPlantLike>(FLOWER, "Poppy"));
    Block::registerBlock(std::make_unique<BlockPlantLike>(BROWN_MUSHROOM, "Brown Mushroom"));
    Block::registerBlock(std::make_unique<BlockPlantLike>(RED_MUSHROOM, "Red Mushroom"));
    Block::registerBlock(std::make_unique<BlockPlantLike>(SUGARCANE, "Sugarcane"));
    Block::registerBlock(std::make_unique<BlockPlantLike>(DOUBLE_PLANT, "Double Plant"));

    Block::registerBlock(std::make_unique<BlockThinDecor>(BED, "Bed Block"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(COBWEB_ID, "Cobweb"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(REDSTONE_WIRE, "Redstone Dust"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(WOODEN_DOOR, "Wooden Door"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(LADDER_ID, "Ladder"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(RAIL, "Rail"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(IRON_DOOR, "Iron Door"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(LEVER, "Lever"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(STONE_PRESSURE_PLATE, "Stone Pressure Plate"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(WOOD_PRESSURE_PLATE, "Wood Pressure Plate"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(STONE_BUTTON, "Stone Button"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(FENCE_ID, "Fence"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(TRAPDOOR_ID, "Trapdoor"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(IRON_BARS_ID, "Iron Bars"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(VINE_ID, "Vines"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(WATER_LILY, "Lily Pad"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(TRAPDOOR_IRON, "Iron Trapdoor"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(LEAVES_2, "Leaves"));
    Block::registerBlock(std::make_unique<BlockThinDecor>(CACTUS_ID, "Cactus"));

    Block::registerBlock(std::make_unique<BlockSolidLike>(WOOL, "Wool"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(DOUBLE_SLAB_ID, "Double Stone Slab"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(BRICKS, "Bricks"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(BOOKSHELF, "Bookshelf"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(MOSSY_COBBLESTONE, "Mossy Cobblestone"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(MOB_SPAWNER, "Mob Spawner"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(WORKBENCH, "Crafting Table"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(COBBLE_STAIRS, "Cobblestone Stairs"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(CLAY, "Clay"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(PUMPKIN_ID, "Pumpkin"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(MONSTER_EGG_BLOCK, "Monster Stone Egg"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(HUGE_MUSHROOM_BROWN, "Brown Mushroom Block"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(HUGE_MUSHROOM_RED, "Red Mushroom Block"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(MELON_BLOCK_ID, "Melon"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(MYCELIUM, "Mycelium"));
    Block::registerBlock(std::make_unique<BlockSolidLike>(WOOD_2, "Log"));
}
} // namespace
