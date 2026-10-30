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
// CurseStone | vgen — block table implementation (0.15 palette).

#include "vgen/Blocks.h"
#include "vgen/VgenWorld.h"
#include "vgen/Facing.h"
#include "vgen/material/Material.h"

namespace vgen {

// ---------------------------------------------------------------------------
// Registry instances — vanilla names, 0.15 (protocol 84) numeric ids.
// ---------------------------------------------------------------------------
static Block gGrass(2);
Block* Block::mGrass = &gGrass;
static Block gWaterStill(9);
Block* Block::mWaterStill = &gWaterStill;
static Block gLavaStill(11);
Block* Block::mLavaStill = &gLavaStill;

// each entry owns one static Block instance and points the registry
// pointer at it (the vanilla registry is a table of Block objects)
#define B(name, id)                     \
        static Block s_block_##name(id); \
        const Block* Block::name = &s_block_##name
B(mAir, 0);
B(mStone, 1);
B(mDirt, 3);
B(mCobblestone, 4);
B(mMossyCobblestone, 48);
B(mWoodPlanks, 5);
B(mSapling, 6);
B(mBedrock, 7);
B(mFlowingWater, 8);
B(mStillWater, 9);
B(mFlowingLava, 10);
B(mStillLava, 11);
B(mSand, 12);
B(mGravel, 13);
B(mGoldOre, 14);
B(mIronOre, 15);
B(mCoalOre, 16);
B(mLog, 17);
B(mLog2, 162);
B(mLeaves, 18);
B(mLeaves2, 161);
B(mGlass, 20);
B(mLapisOre, 21);
B(mDispenser, 23);
B(mStickyPiston, 29);
B(mSandStone, 24);
B(mWeb, 30);
B(mTallgrass, 31);
B(mDeadBush, 32);
B(mWool, 35);
B(mYellowFlower, 37);
B(mRedFlower, 38);
B(mBrownMushroom, 39);
B(mRedMushroom, 40);
B(mGoldBlock, 41);
B(mIronBlock, 42);
B(mDoubleStoneSlab, 43);
B(mStoneSlab, 44);
B(mBricks, 45);
B(mTNT, 46);
B(mBookshelf, 47);
B(mObsidian, 49);
B(mTorch, 50);
B(mFire, 51);
B(mMobSpawner, 52);
B(mOakStairs, 53);
B(mChest, 54);
B(mRedStoneDust, 55);
B(mDiamondOre, 56);
B(mDiamondBlock, 57);
B(mWorkBench, 58);
B(mWheatCrop, 59);
B(mFarmland, 60);
B(mFurnace, 61);
B(mLadder, 65);
B(mRail, 66);
B(mStoneStairs, 67);
B(mLever, 69);
B(mStonePressurePlate, 70);
B(mRedStoneOre, 73);
B(mStoneButton, 77);
B(mWoodButton, 143);
B(mSnow, 78);
B(mIce, 79);
B(mCactus, 81);
B(mClay, 82);
B(mReeds, 83);
B(mFence, 85);
B(mPumpkin, 86);
B(mNetherrack, 87);
B(mSoulSand, 88);
B(mGlowStone, 89);
B(mTrapdoor, 96);
B(mIronTrapdoor, 167);
B(mMonsterStoneEgg, 97);
B(mStoneBrick, 98);
B(mBrownMushroomBlock, 99);
B(mRedMushroomBlock, 100);
B(mIronFence, 101);
B(mGlassPane, 102);
B(mMelon, 103);
B(mVine, 106);
B(mMycelium, 110);
B(mWaterlily, 111);
B(mNetherBrick, 112);
B(mNetherFence, 113);
B(mEnchantingTable, 116);
B(mCauldron, 118);
B(mEndStone, 121);
B(mEnderChest, 130);
B(mEmeraldOre, 129);
B(mTripwireHook, 131);
B(mTripwire, 132);
B(mSandstoneStairs, 128);
B(mSpruceStairs, 134);
B(mAcaciaStairs, 163);
B(mUnpoweredRepeater, 93);
B(mStainedClay, 159);
B(mHardenedClay, 172);
B(mPodzol, 243);
B(mGrassPathBlock, 198);
B(mPackedIce, 174);
B(mWoolCarpet, 171);
B(mDoublePlant, 175);
B(mCocoa, 127);
B(mRedSandstone, 179);
B(mCarrotCrop, 141);
B(mPotatoCrop, 142);
B(mBeetrootCrop, 244);
B(mWoodenDoor, 64);
B(mIronDoor, 71);
B(mWoodenDoorSpruce, 193);
B(mWoodenDoorAcacia, 196);
B(mFlowerPot, 140);
#undef B


// ---------------------------------------------------------------------------
// Static tables
// ---------------------------------------------------------------------------
const Block* Block::mBlocks[Block::NUM_BLOCK_TYPES] = {};
bool Block::mSolid[Block::NUM_BLOCK_TYPES] = {};
bool Block::mShouldTick[Block::NUM_BLOCK_TYPES] = {};
float Block::mTranslucency[Block::NUM_BLOCK_TYPES] = {};
uint8_t Block::mLightBlock[Block::NUM_BLOCK_TYPES] = {};
uint8_t Block::mLightEmission[Block::NUM_BLOCK_TYPES] = {};

static MaterialType sMaterialType[Block::NUM_BLOCK_TYPES];

// Blocks whose material blocks motion — the vanilla "solid" set the
// generator queries through mSolid[] and material checks.
static bool isSolidBlockingMaterial(MaterialType t) {
        switch (t) {
                case MaterialType::Air:
                case MaterialType::Water:
                case MaterialType::Lava:
                case MaterialType::Leaves:
                case MaterialType::Plant:
                case MaterialType::ReplaceablePlant:
                case MaterialType::TopSnow:
                case MaterialType::Fire:
                case MaterialType::Web:
                case MaterialType::RedstoneWire:
                case MaterialType::Portal:
                case MaterialType::Snow:
                        return false;
                default:
                        return true;
        }
}

static void setMaterial(int lo, int hi, MaterialType t) {
        for (int id = lo; id <= hi; ++id) sMaterialType[id] = t;
}

static void initMaterialTable() {
        setMaterial(0, 255, MaterialType::Stone);
        auto set = [](std::initializer_list<int> ids, MaterialType t) {
                for (int id : ids) sMaterialType[id] = t;
        };
        sMaterialType[0] = MaterialType::Air;
        set({8, 9}, MaterialType::Water);
        set({10, 11}, MaterialType::Lava);
        set({3, 60, 243, 198}, MaterialType::Dirt);
        set({82}, MaterialType::Clay);
        set({2, 110}, MaterialType::Dirt); // grass + mycelium (earth surface)
        set({5, 17, 162, 85, 53, 67, 64, 54, 47, 58, 63, 68, 84, 107, 126, 125, 134, 135, 136, 163, 164, 108, 109, 114, 128, 157, 158, 96, 143, 145, 147, 148, 151, 154, 23, 29, 33, 34, 36}, MaterialType::Wood);
        set({1, 4, 7, 14, 15, 16, 21, 22, 24, 43, 44, 45, 48, 49, 56, 57, 61, 73, 74, 87, 112, 97, 98, 99, 100, 121, 123, 124, 129, 139, 152, 153, 155, 156, 159, 172, 173, 174, 179, 180, 245, 246}, MaterialType::Stone);
        set({41, 42, 71, 101, 118, 130, 138}, MaterialType::Metal);
        set({18, 161}, MaterialType::Leaves);
        set({6, 37, 38, 39, 40, 59, 83, 111, 127, 141, 142, 175}, MaterialType::Plant);
        set({31, 32, 106}, MaterialType::ReplaceablePlant);
        set({12, 13, 88}, MaterialType::Sand);
        set({20, 95, 102}, MaterialType::Glass);
        set({79}, MaterialType::Ice);
        set({78}, MaterialType::TopSnow);
        set({80}, MaterialType::Snow);
        set({30}, MaterialType::Web);
        set({35}, MaterialType::Cloth);
        set({171}, MaterialType::Carpet);
        set({50, 65, 66, 27, 28, 55}, MaterialType::Decoration);
        sMaterialType[55] = MaterialType::RedstoneWire;
        set({46}, MaterialType::Explosive);
        set({51}, MaterialType::Fire);
        set({19}, MaterialType::Sponge);
        set({165}, MaterialType::Slime);
        set({90, 119, 120}, MaterialType::Portal);
        set({92}, MaterialType::Cake);
        set({52}, MaterialType::Stone);
}

void Block::initBlocks() {
        // fill the registry from the declared constants
        const Block* const reg[] = {
                mAir, mStone, mGrass, mDirt, mCobblestone, mMossyCobblestone,
                mWoodPlanks, mSapling, mBedrock, mFlowingWater, mStillWater,
                mFlowingLava, mStillLava, mSand, mGravel, mGoldOre, mIronOre,
                mCoalOre, mLog, mLog2, mLeaves, mLeaves2, mGlass, mLapisOre,
                mDispenser, mSandStone, mWeb, mTallgrass, mDeadBush, mWool,
                mYellowFlower, mRedFlower, mBrownMushroom, mRedMushroom, mGoldBlock,
                mIronBlock, mDoubleStoneSlab, mStoneSlab, mBricks, mTNT, mBookshelf,
                mObsidian, mTorch, mFire, mMobSpawner, mOakStairs, mChest,
                mRedStoneDust, mDiamondOre, mDiamondBlock, mWorkBench, mWheatCrop,
                mFarmland, mFurnace, mLadder, mRail, mStoneStairs, mLever,
                mStonePressurePlate, mRedStoneOre, mStoneButton, mWoodButton, mSnow,
                mIce, mCactus, mClay, mReeds, mFence, mPumpkin, mNetherrack,
                mSoulSand, mGlowStone, mTrapdoor, mIronTrapdoor, mMonsterStoneEgg,
                mStoneBrick, mBrownMushroomBlock, mRedMushroomBlock, mIronFence,
                mGlassPane, mMelon, mVine, mMycelium, mWaterlily, mNetherBrick,
                mNetherFence, mEnchantingTable, mCauldron, mEndStone, mEnderChest,
                mEmeraldOre, mTripwireHook, mTripwire, mSandstoneStairs,
                mSpruceStairs, mAcaciaStairs, mUnpoweredRepeater, mStainedClay,
                mHardenedClay, mPodzol, mGrassPathBlock, mPackedIce, mWoolCarpet,
                mDoublePlant, mCocoa, mRedSandstone, mCarrotCrop, mPotatoCrop,
                mBeetrootCrop, mWoodenDoor, mIronDoor, mWoodenDoorSpruce,
                mWoodenDoorAcacia, mFlowerPot, mWaterStill, mLavaStill,
        };
        initMaterialTable();
        for (int id = 0; id < NUM_BLOCK_TYPES; ++id) {
                mBlocks[id] = mAir; // unmapped ids fall through to air
        }
        for (const Block* b : reg) {
                if (b) mBlocks[b->mID] = b;
        }

        for (int id = 0; id < NUM_BLOCK_TYPES; ++id) {
                mSolid[id] = isSolidBlockingMaterial(sMaterialType[id]);
                // leaves block motion in vanilla but stay light-translucent
                mLightBlock[id] = mSolid[id] ? 15 : 0;
                mLightEmission[id] = 0;
        }
        mLightBlock[9] = 2;  // still water
        mLightBlock[8] = 2;  // flowing water
        mLightBlock[18] = 1; // leaves
        mLightBlock[161] = 1;
        mLightBlock[79] = 3; // ice
        mLightBlock[20] = 0; // glass
        mLightBlock[102] = 0;
        mLightEmission[89] = 15;  // glowstone
        mLightEmission[50] = 14;  // torch
        mLightEmission[10] = 15;  // lava
        mLightEmission[11] = 15;
        mLightEmission[51] = 15;  // fire
        mLightEmission[91] = 15;  // lit pumpkin
}

// ---------------------------------------------------------------------------
// Per-block behaviour
// ---------------------------------------------------------------------------
static BlockStateRef stateFor(const Block* b, BlockState state) {
        switch (b->mID) {
                case 17:  // log
                case 162: // log2
                        if (state == BlockState::MappedType) return BlockStateRef(0, 3);
                        if (state == BlockState::Direction) return BlockStateRef(2, 3);
                        break;
                case 3: // dirt — coarse bit
                        if (state == BlockState::MappedType) return BlockStateRef(0, 1);
                        break;
                case 106: // vine
                        if (state == BlockState::VineGrowth) return BlockStateRef(0, 15);
                        break;
                case 132: // tripwire
                        if (state == BlockState::Direction) return BlockStateRef(0, 3);
                        if (state == BlockState::AttachedBit) return BlockStateRef(2, 1);
                        break;
                default:
                        break;
        }
        return BlockStateRef(0, 0); // no-op for unmapped states
}

BlockStateRef Block::getBlockState(BlockState state) const {
        return stateFor(this, state);
}

bool DoublePlantBlock::placeAt(BlockSource& region, const BlockPos& pos, int type,
                               int flags, Entity* placer) const {
        // lower half carries the plant type; upper half adds the top bit
        region.setBlockAndData(pos, 175, (DataID)(DataID)type, flags, placer);
        region.setBlockAndData(pos.above(), 175, (DataID)((DataID)type | 8), flags, placer);
        return true;
}

const Block& FullBlock::getBlock() const {
        return *Block::mBlocks[id.value];
}

bool BlockID::hasProperty(BlockProperty prop) const {
        // leaf check drives the generator's canopy descents; everything
        // else is decorative for worldgen purposes
        if (prop == BlockProperty::Leaf)
                return value == 18 || value == 161;
        return false;
}

bool Block::hasProperty(BlockProperty prop) const {
        return mID.hasProperty(prop);
}

const Material& Block::getMaterial() const {
        return Material::getMaterial(sMaterialType[mID]);
}

bool Block::mayPlaceOn(const Block& below) const {
        // vanilla BushBlock::mayPlaceOn — grass, dirt, farmland, podzol.
        // (used by TreeFeature::_prepareSpawn through the sapling and by
        // the double-plant feature; the generator's soil set.)
        return below.mID == 2 || below.mID == 3 || below.mID == 60 || below.mID == 88;
}

bool Block::mayPlaceOn(BlockSource& region, const BlockPos& pos) const {
        // "on" = pos is the block ABOVE the support; sapling-style check.
        BlockID below = region.getBlockID(pos.below());
        return below == 2 || below == 3 || below == 60 || below == 88; // grass/dirt/farmland/podzol
}

bool Block::canSurvive(BlockSource& region, const BlockPos& pos) const {
        // Downport of the vanilla per-block survival checks the decoration
        // features rely on (BushBlock/TallGrass/DeadBush/MushroomBlock/
        // ReedBlock/CactusBlock/WaterlilyBlock::canSurvive). Without these
        // the decorators happily float grass, flowers and dead bushes in
        // mid-air wherever the random Y landed.
        switch (mID) {
                case 6:  // sapling — BushBlock
                case 31: // tall grass — BushBlock
                case 37: // yellow flower — BushBlock
                case 38: // red flower — BushBlock
                case 175: // double plant — BushBlock
                        return mayPlaceOn(region, pos);
                case 32: { // dead bush — sand / hardened clay / stained clay / podzol
                        BlockID below = region.getBlockID(pos.below());
                        return below == 12 || below == 172 || below == 159 || below == 88;
                }
                case 39: // mushrooms — podzol/mycelium, or dark spot over solid ground
                case 40: {
                        if (pos.y < 0 || pos.y >= region.getMaxHeight())
                                return false;
                        BlockID below = region.getBlockID(pos.below());
                        if (below == 88 || below == 110) // podzol or mycelium
                                return true;
                        return region.getBrightness(pos) < 13 && Block::mSolid[below & 0xFF];
                }
                case 81: { // cactus — sand/cactus below, free sides
                        BlockID below = region.getBlockID(pos.below());
                        if (below != 12 && below != 81) return false; // sand or cactus
                        for (int face = 2; face <= 5; ++face) {
                                BlockPos n = pos.offset(Facing::DIRECTION[face].x, 0,
                                                        Facing::DIRECTION[face].z);
                                if (mBlocks[region.getBlockID(n)]->getMaterial().getBlocksMotion())
                                        return false;
                        }
                        return true;
                }
                case 83: { // reeds — vanilla ReedBlock::mayPlace
                        BlockID below = region.getBlockID(pos.below());
                        if (below == mID)
                                return true;
                        if (below != 2 && below != 3 && below != 12) // grass/dirt/sand
                                return false;
                        const Material& water = Material::getMaterial(MaterialType::Water);
                        if (region.getMaterial(pos.offset(-1, -1, 0)) == water) return true;
                        if (region.getMaterial(pos.offset(1, -1, 0)) == water) return true;
                        if (region.getMaterial(pos.offset(0, -1, -1)) == water) return true;
                        if (region.getMaterial(pos.offset(0, -1, 1)) == water) return true;
                        return false;
                }
                case 111: { // waterlily — floats on still/flowing water
                        BlockID below = region.getBlockID(pos.below());
                        return below == 8 || below == 9;
                }
                default:
                        return true;
        }
}

bool Block::mayPlace(BlockSource& region, const BlockPos& pos, FacingID face) const {
        switch (mID) {
                case 6: // sapling
                        return mayPlaceOn(region, pos);
                case 106: { // vine — needs a solid neighbour on the faced side
                        if (face < 2 || face > 5) return false;
                        BlockPos n = pos.offset(Facing::DIRECTION[face].x,
                                                Facing::DIRECTION[face].y,
                                                Facing::DIRECTION[face].z);
                        const Block* nb = mBlocks[region.getBlockID(n)];
                        return nb->getMaterial().getBlocksMotion() && !nb->getMaterial().isLiquid();
                }
                case 111: { // waterlily — floats on still water
                        BlockID below = region.getBlockID(pos.below());
                        return below == 8 || below == 9;
                }
                case 175: // double plant on grass/dirt
                        return mayPlaceOn(region, pos);
                case 86:  // pumpkin
                case 103: // melon
                        return mayPlaceOn(region, pos);
                default:
                        return true;
        }
}

} // namespace vgen
