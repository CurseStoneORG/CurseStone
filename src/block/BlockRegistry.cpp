// BlockRegistry — registers every concrete Block subclass into the
// Block::get() flyweight map. Called once at Server startup (phase 21).
#include "Block.h"
#include "BlockAir.h"
#include "BlockBedrock.h"
#include "BlockChest.h"
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
#include "BlockLeaves.h"
#include "BlockLog.h"
#include "BlockMagmaBlock.h"
#include "BlockNetherrack.h"
#include "BlockObsidian.h"
#include "BlockPlanks.h"
#include "BlockRedstoneOre.h"
#include "BlockSapling.h"
#include "BlockSand.h"
#include "BlockSandstone.h"
#include "BlockSign.h"
#include "BlockSlab.h"
#include "BlockSnow.h"
#include "BlockSoulSand.h"
#include "BlockStone.h"
#include "BlockStoneBricks.h"
#include "BlockTNT.h"
#include "BlockTorch.h"
#include "BlockWater.h"
#include "BlockWheat.h"
#include "BlockWood.h"
#include "BlockWoodStairs.h"
#include "BlockGlowstone.h"
#include "BlockIds.h"
#include <memory>
namespace cursestone::block {
namespace {
template <typename T> std::unique_ptr<T> make_id() { return std::make_unique<T>(); }
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
    Block::registerBlock(std::make_unique<BlockGlowstone>());
    Block::registerBlock(std::make_unique<BlockNetherrack>());
    Block::registerBlock(std::make_unique<BlockSoulSand>());
    Block::registerBlock(std::make_unique<BlockObsidian>());
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
    Block::registerBlock(std::make_unique<BlockWater>());
    Block::registerBlock(std::make_unique<BlockWood>());
    Block::registerBlock(std::make_unique<BlockWoodStairs>());
    Block::registerBlock(std::make_unique<BlockStoneBricks>());
    Block::registerBlock(std::make_unique<BlockMagmaBlock>());
}
} // namespace
