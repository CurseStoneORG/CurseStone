// CurseStone | vgen — block table shim.
//
// The restored vanilla generator references Mojang's Block registry
// (`Block::mStone->mID`, `Block::mBlocks[id]->getMaterial()`...). This
// header re-declares that registry surface on top of the MCPE 0.15
// (protocol 84) block palette, so the ported generator code needed no
// changes: every constant keeps its vanilla name and stores the 0.15
// numeric id it maps to.
//
// Downport notes (1.0.0 -> 0.15): the 1.0 BlockState model is emulated
// for the handful of states the generator writes (log axis/type, dirt
// coarse bit, vine growth, tripwire attach) — each maps onto plain
// 0.15 metadata bits.
#pragma once
#include "vgen/Vanilla.h"
#include "vgen/Random.h"
#include "vgen/material/MaterialType.h"

namespace vgen {

class BlockSource;
class Material;
class Block;
class Entity;

// BlockState shim — 1.0 wrote `block->getBlockState(BlockState::X).set(data, v)`.
// On 0.15 every state is plain metadata, so the shim folds the value into
// `data` at the state's bit position using a per-(block,state) table.
enum class BlockState : int {
	MappedType,   // log type / dirt coarse bit — low bits
	Direction,    // log axis (<<2) or tripwire facing (<<0)
	VineGrowth,   // vine face bits 1/2/4/8 — identity
	AttachedBit,  // tripwire attached (<<2)
};

class BlockStateRef {
public:
	BlockStateRef(uint8_t shift, uint8_t mask) : mShift(shift), mMask(mask) {}
	void set(DataID& data, int value) const {
		data = (uint8_t)((data & ~(mMask << mShift)) | ((value & mMask) << mShift));
	}

private:
	uint8_t mShift, mMask;
};

class Block {
public:
	// block update flags (Block.h verbatim)
	static const int UPDATE_NEIGHBORS = (1 << 0);
	static const int UPDATE_CLIENTS = (1 << 1);
	static const int UPDATE_INVISIBLE = (1 << 2);
	static const int UPDATE_ITEM_DATA = (1 << 4);
	static const int UPDATE_NONE = UPDATE_INVISIBLE;
	static const int UPDATE_ALL = UPDATE_NEIGHBORS | UPDATE_CLIENTS;

	static const int NUM_BLOCK_TYPES = 256;

	static const Block* mBlocks[NUM_BLOCK_TYPES];
	static bool mSolid[NUM_BLOCK_TYPES];
	static bool mShouldTick[NUM_BLOCK_TYPES];
	static float mTranslucency[NUM_BLOCK_TYPES];
	static uint8_t mLightBlock[NUM_BLOCK_TYPES];
	static uint8_t mLightEmission[NUM_BLOCK_TYPES];

	static void initBlocks();

	Block(int id, int data = 0) : mID((uint8_t)id), mData((uint8_t)data) {}

	BlockID mID;
	uint8_t mData;

	const Material& getMaterial() const;
	bool mayPlace(BlockSource& region, const BlockPos& pos, FacingID face) const;
	bool mayPlace(BlockSource& region, const BlockPos& pos) const {
		return mayPlace(region, pos, (FacingID)1);
	}
	bool mayPlaceOn(BlockSource& region, const BlockPos& pos) const;
	bool mayPlaceOn(const Block& below) const;
	bool canSurvive(BlockSource& region, const BlockPos& pos) const;
	BlockStateRef getBlockState(BlockState state) const;
	bool isType(const Block* other) const { return mID == other->mID; }
	bool hasProperty(BlockProperty prop) const;
	bool isStairBlock() const {
		return mID == 53 || mID == 67 || mID == 108 || mID == 109 || mID == 114 ||
		       mID == 128 || mID == 134 || mID == 135 || mID == 136 || mID == 156 ||
		       mID == 163 || mID == 164;
	}
	bool isSolid() const { return mSolid[mID]; }
	// fluid/leaf ticks fired at generation time; the server sim handles them
	void tick(BlockSource&, const BlockPos&, Random&) const {}

	// Registry — vanilla names, 0.15 ids.
	static const Block* mAir;
	static const Block* mStone;
	static Block* mGrass;
	static const Block* mDirt;
	static const Block* mCobblestone;
	static const Block* mMossyCobblestone;
	static const Block* mWoodPlanks;
	static const Block* mSapling;
	static const Block* mBedrock;
	static const Block* mFlowingWater;
	static const Block* mStillWater;
	static const Block* mFlowingLava;
	static const Block* mStillLava;
	static const Block* mSand;
	static const Block* mGravel;
	static const Block* mGoldOre;
	static const Block* mIronOre;
	static const Block* mCoalOre;
	static const Block* mLog;
	static const Block* mLog2;
	static const Block* mLeaves;
	static const Block* mLeaves2;
	static const Block* mGlass;
	static const Block* mLapisOre;
	static const Block* mDispenser;
	static const Block* mStickyPiston;
	static const Block* mSandStone;
	static const Block* mWeb;
	static const Block* mTallgrass;
	static const Block* mDeadBush;
	static const Block* mWool;
	static const Block* mYellowFlower;
	static const Block* mRedFlower;
	static const Block* mBrownMushroom;
	static const Block* mRedMushroom;
	static const Block* mGoldBlock;
	static const Block* mIronBlock;
	static const Block* mDoubleStoneSlab;
	static const Block* mStoneSlab;
	static const Block* mBricks;
	static const Block* mTNT;
	static const Block* mBookshelf;
	static const Block* mObsidian;
	static const Block* mTorch;
	static const Block* mFire;
	static const Block* mMobSpawner;
	static const Block* mOakStairs;
	static const Block* mChest;
	static const Block* mRedStoneDust;
	static const Block* mDiamondOre;
	static const Block* mDiamondBlock;
	static const Block* mWorkBench;
	static const Block* mWheatCrop;
	static const Block* mFarmland;
	static const Block* mFurnace;
	static const Block* mLadder;
	static const Block* mRail;
	static const Block* mStoneStairs;
	static const Block* mLever;
	static const Block* mStonePressurePlate;
	static const Block* mRedStoneOre;
	static const Block* mStoneButton;
	static const Block* mWoodButton;
	static const Block* mSnow;
	static const Block* mIce;
	static const Block* mCactus;
	static const Block* mClay;
	static const Block* mReeds;
	static const Block* mFence;
	static const Block* mPumpkin;
	static const Block* mNetherrack;
	static const Block* mSoulSand;
	static const Block* mGlowStone;
	static const Block* mTrapdoor;
	static const Block* mIronTrapdoor;
	static const Block* mMonsterStoneEgg;
	static const Block* mStoneBrick;
	static const Block* mBrownMushroomBlock;
	static const Block* mRedMushroomBlock;
	static const Block* mIronFence;
	static const Block* mGlassPane;
	static const Block* mMelon;
	static const Block* mVine;
	static const Block* mMycelium;
	static const Block* mWaterlily;
	static const Block* mNetherBrick;
	static const Block* mNetherFence;
	static const Block* mEnchantingTable;
	static const Block* mCauldron;
	static const Block* mEndStone;
	static const Block* mEnderChest;
	static const Block* mEmeraldOre;
	static const Block* mTripwireHook;
	static const Block* mTripwire;
	static const Block* mSandstoneStairs;
	static const Block* mSpruceStairs;
	static const Block* mAcaciaStairs;
	static const Block* mUnpoweredRepeater;
	static const Block* mStainedClay;
	static const Block* mHardenedClay;
	static const Block* mPodzol;
	static const Block* mGrassPathBlock;
	static const Block* mPackedIce;
	static const Block* mWoolCarpet;
	static const Block* mDoublePlant;
	static const Block* mCocoa;
	static const Block* mRedSandstone;
	static const Block* mCarrotCrop;
	static const Block* mPotatoCrop;
	static const Block* mBeetrootCrop;
	static const Block* mWoodenDoor;
	static const Block* mIronDoor;
	static const Block* mWoodenDoorSpruce;
	static const Block* mWoodenDoorAcacia;
	static const Block* mFlowerPot;
	static Block* mWaterStill;
	static Block* mLavaStill;
};

// Subclass constant shims — the generator references these exactly as
// the 1.0 block subclasses exposed them.
class RotatedPillarBlock {
public:
	static const int FACING_Y = 0;
	static const int FACING_X = 1;
	static const int FACING_Z = 2;
};
using LogBlock = RotatedPillarBlock;

class VineBlock {
public:
	// 1 << Direction::FACING_DIRECTION[face]: S=1, W=2, N=4, E=8
	static const int VINE_SOUTH = 1;
	static const int VINE_WEST = 2;
	static const int VINE_NORTH = 4;
	static const int VINE_EAST = 8;
	static const int VINE_ALL = 15;
};

class TorchBlock {
public:
	static const int TORCH_UNKNOWN = 0;
	static const int TORCH_WEST = 1;
	static const int TORCH_EAST = 2;
	static const int TORCH_NORTH = 3;
	static const int TORCH_SOUTH = 4;
	static const int TORCH_TOP = 5;
};

class FlowerBlock {
public:
	enum class ROSES : unsigned char {
		POPPY = 0,
		ORCHID,
		ALLIUM,
		HOUSTONIA,
		TULIP_RED,
		TULIP_ORANGE,
		TULIP_WHITE,
		TULIP_PINK,
		OXEYE,
		_count
	};
};

class OldLogBlock {
public:
	enum class LogType : DataID {
		Oak = 0,
		Spruce,
		Birch,
		Jungle
	};
};

class OldLeafBlock {
public:
	enum class LeafType : DataID {
		Oak = 0,
		Spruce,
		Birch,
		Jungle
	};
};

class TallGrassTypeShim {
public:
	enum class TallGrassType : DataID {
		Tall = 1,
		Fern,
		Snow
	};
};
using TallGrassType = TallGrassTypeShim::TallGrassType;

class StoneBlock {
public:
	enum class StoneType : DataID {
		Stone = 0,
		Granite = 1,
		GraniteSmooth = 2,
		Diorite = 3,
		DioriteSmooth = 4,
		Andesite = 5,
		AndesiteSmooth = 6
	};
};

class NewLogBlock {
public:
	static const int ACACIA_TRUNK = 0;
	static const int BIG_OAK_TRUNK = 1;
};

class LeafBlock {
public:
	static const int NORMAL_LEAF = 0;
};

class NewLeafBlock {
public:
	static const int ACACIA_LEAF = 0;
	static const int BIG_OAK_LEAF = 1;
	static const int NUM_TYPES = 2;
};

class DirtBlock {
public:
	enum DirtType : unsigned char {
		TYPE_NORMAL = 0,
		TYPE_NO_GRASS_SPREAD
	};
};

class WoodBlockTypeShim {
public:
	enum class WoodBlockType : DataID {
		Oak = 0,
		Spruce = 1,
		Birch = 2,
		Jungle = 3,
		Acacia = 4,
		Big_Oak = 5
	};
};
using WoodBlockType = WoodBlockTypeShim::WoodBlockType;

class BushBlock : public Block {
public:
	BushBlock() : Block(0) {}
};

class StoneSlabBlock {
public:
	enum class StoneSlabType {
		Stone = 0,
		Sandstone = 1,
		Wood = 2,
		Cobblestone = 3,
		Brick = 4,
		StoneBrick = 5,
		Quartz = 6,
		Netherbrick = 7,
		_count
	};
};

class SandStoneBlock {
public:
	enum class SandstoneType {
		Default = 0,
		Heiroglyphs = 1,
		Smoothside = 2
	};
};

class DoublePlantBlock : public Block {
public:
	DoublePlantBlock() : Block(175) {}
	enum class DoublePlantType : DataID {
		Sunflower = 0,
		Syringa = 1,
		Grass = 2,
		Fern = 3,
		Rose = 4,
		Paeonia = 5,
		Count = 6
	};

	bool placeAt(BlockSource& region, const BlockPos& pos, int type,
	             int flags, Entity* placer) const;
};
using DoublePlantType = DoublePlantBlock::DoublePlantType;

class SandBlock {
public:
	enum class SandType : DataID {
		Normal = 0,
		Red = 1
	};
};

class CauldronBlock {
public:
	static const int MIN_FILL_LEVEL = 0;
	static const int MAX_FILL_LEVEL = 6;
};

class BaseRailBlock {
public:
	static const DataID DIR_FLAT_Z = 0;
	static const DataID DIR_FLAT_X = 1;
};

} // namespace vgen
