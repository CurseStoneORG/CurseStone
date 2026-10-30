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

#include "vgen/biome/MesaBiome.h"
#include "vgen/Blocks.h"
#include "vgen/Blocks.h"
#include "vgen/biome/BiomeDecorator.h"
#include "vgen/synth/PerlinSimplexNoise.h"
#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
//#include "world/item/DyePowderItem.h"
#include "vgen/material/Material.h"
#include "vgen/Vanilla.h"

#include <cmath>
#include <cstring>

namespace vgen {

namespace {

// Stained-clay aux values (0.15 metadata layout).
constexpr DataID CLAY_WHITE  = 0;
constexpr DataID CLAY_ORANGE = 1;
constexpr DataID CLAY_YELLOW = 4;
constexpr DataID CLAY_SILVER = 8;
constexpr DataID CLAY_BROWN  = 12;
constexpr DataID CLAY_RED    = 14;

// Band-table marker: a band at or above this value paints as plain
// hardened clay instead of a stained color.
constexpr DataID CLAY_NO_COLOR = 16;

} // namespace

MesaBiome::MesaBiome(int id, bool brycePillars, bool hasForest) :
	Biome(id, Biome::BiomeType::Mesa, make_unique<Decorator>())
	, brycePillars(brycePillars)
	, hasForest(hasForest) {
	setNoRain();
	setTemperatureAndDownfall(2, 0);

	_friendlies.clear();
	mTopMaterial = Block::mSand->mID;
	mTopMaterialData = enum_cast(SandBlock::SandType::Red);
	mMaterial = Block::mHardenedClay->mID;

	decorator->treeCount = -999;
	decorator->deadBushCount = 20;
	decorator->reedsCount = 3;
	decorator->cactusCount = 5;
	decorator->flowerCount = 0;

	if(hasForest) {
		decorator->treeCount = 5;
	}
}

const FeaturePtr& MesaBiome::getTreeFeature(Random* random){
	return decorator->oakFeature;
}

int MesaBiome::getFoliageColor() {
	return 0xffaea42a;
}

int MesaBiome::getMapFoliageColor() {
	return getFoliageColor();
}

int MesaBiome::getGrassColor( const BlockPos& pos ) {
	return 0xff90814d;
}

int MesaBiome::getMapGrassColor(const BlockPos& pos) {
	return getGrassColor(pos);
}

void MesaBiome::refreshBiome(RandomSeed levelSeed) {
	generateBands(levelSeed);
	Random r(levelSeed);
	pillarNoise = make_unique<PerlinSimplexNoise>(r, 4);
	pillarRoofNoise = make_unique<PerlinSimplexNoise>(r, 1);
}

// ---------------------------------------------------------------------------
// Mesa surface: hardened-clay body with colored stained-clay bands, a red
// sand cover near sea level and grass / coarse dirt on the forest plateau
// tops. The band sequence repeats vertically every 64 blocks, shifted by a
// slow noise field so neighboring columns slide the stripes up and down.
// ---------------------------------------------------------------------------
void MesaBiome::buildSurfaceAt(Random& random, BlockVolume& blocks, const BlockPos& pos, float depthValue, Height seaLevel) {

	_placeBedrock(random, blocks, pos);

	// Bryce pillar shaping stays disabled; the surrounding terrain pass
	// already delivers the plateau heights this surface expects.
	auto p = (ChunkBlockPos)pos;

	const BlockID clay = Block::mStainedClay->mID;
	const BlockID hardClay = Block::mHardenedClay->mID;
	BlockID top = clay;
	BlockID material = mMaterial;

	int runDepth = (int)(depthValue / 3 + 3 + random.nextFloat() * 0.25f);
	bool claySurface = std::cos(depthValue / 3.0f * Math::PI) > 0.0f;
	int run = -1;
	bool sandRun = false;
	int clayDepth = 0;

	for(p.y = blocks.getHeight() - 1; p.y > 0; p.y--) {

		auto old = blocks.block(p);

		if (old == Block::mBedrock->mID) {
			break;
		}

		if (clayDepth < 15) {
			if(old == 0) {
				run = -1;
			}
			else if (old == Block::mStone->mID) {
				if (run == -1) {
					sandRun = false;
					if (runDepth <= 0) {
						top = BlockID::AIR;
						material = Block::mStone->mID;
					}
					else if (p.y >= seaLevel - 4 && p.y <= seaLevel + 1) {
						top = clay;
						material = mMaterial;
					}

					if (p.y < seaLevel && top == 0) {
						top = Block::mStillWater->mID;
					}

					run = runDepth + std::max(0, p.y - seaLevel);

					if (p.y >= seaLevel - 1) {
						if (hasForest && p.y > 86 + runDepth * 2) {
							if (claySurface) {
								// coarse dirt caps the forest plateaus
								blocks.setBlock(p, Block::mDirt->mID);

								DataID data = 0;
								Block::mDirt->getBlockState(BlockState::MappedType).set(data, DirtBlock::TYPE_NO_GRASS_SPREAD);
								blocks.setData(p, data);
							}
							else {
								blocks.setBlock(p, Block::mGrass->mID);
							}
						}
						else if (p.y > seaLevel + 3 + runDepth) {
							DataID d = CLAY_NO_COLOR;
							if (p.y < 64 || p.y > 127) {
								d = CLAY_ORANGE;
							}
							else if (claySurface) {
								// bands suppressed on clay-surface columns
							}
							else {
								d = getBand(pos.x, p.y, pos.z);
							}
							if (d < CLAY_NO_COLOR) {
								blocks.setBlock(p, clay);
								blocks.setData(p, d);
							}
							else {
								blocks.setBlock(p, hardClay);
							}
						}
						else {
							blocks.setBlock(p, mTopMaterial);
							blocks.setData(p, mTopMaterialData);
							sandRun = true;
						}
					}
					else {
						blocks.setBlock(p, material);
						if (material == clay) {
							blocks.setData(p, CLAY_ORANGE);
						}
					}
				}
				else if (run > 0) {
					run--;

					if (sandRun) {
						blocks.setBlock(p, clay);
						blocks.setData(p, CLAY_ORANGE);
					}
					else {
						DataID d = getBand(pos.x, p.y, pos.z);
						if (d < CLAY_NO_COLOR) {
							blocks.setBlock(p, clay);
							blocks.setData(p, d);
						}
						else {
							blocks.setBlock(p, hardClay);
						}
					}
				}

				++clayDepth;
			}
		}
	}
}

void MesaBiome::generateBands(RandomSeed seed) {
	memset(clayBands, CLAY_NO_COLOR, sizeof(clayBands));
	Random random(seed);
	clayBandsOffsetNoise = make_unique<PerlinSimplexNoise>(random, 1);

	for (int i = 0; i < 64; ++i) {
		i += random.nextInt(5) + 1;
		if(i < 64) {
			clayBands[i] = CLAY_ORANGE;
		}
	}

	int yellowBands = random.nextInt(4) + 2;

	for (auto i : range(yellowBands)) {
		UNUSED_VARIABLE(i);
		int w = random.nextInt(3) + 1;
		int s = random.nextInt(64);

		for(int p = 0; (s + p) < 64 && p < w; p++) {
			clayBands[s + p] = CLAY_YELLOW;
		}
	}

	int brownBands = random.nextInt(4) + 2;

	for (auto i : range(brownBands)) {
		UNUSED_VARIABLE(i);
		int w = random.nextInt(3) + 2;
		int s = random.nextInt(64);

		for(int p = 0; (s + p) < 64 && p < w; p++) {
			clayBands[s + p] = CLAY_BROWN;
		}
	}

	int redBands = random.nextInt(4) + 2;

	for (auto i : range(redBands)) {
		UNUSED_VARIABLE(i);
		int w = random.nextInt(3) + 1;
		int s = random.nextInt(64);

		for(int p = 0; (s + p) < 64 && p < w; p++) {
			clayBands[s + p] = CLAY_RED;
		}
	}

	int whiteBands = random.nextInt(3) + 3;
	int s = 0;

	for (auto i : range(whiteBands)) {
		UNUSED_VARIABLE(i);
		int w = 1;
		s += random.nextInt(16) + 4;

		for(int p = 0; (s + p) < 64 && p < w; p++) {
			clayBands[s + p] = CLAY_WHITE;
			if(s + p > 1 && random.nextBoolean()) {
				clayBands[s + p - 1] = CLAY_SILVER;
			}
			if(s + p < 63 && random.nextBoolean()) {
				clayBands[s + p + 1] = CLAY_SILVER;
			}
		}
	}
}

unsigned char MesaBiome::getBand(int worldX, int y, int worldZ) {
	int offset = (int)nearbyint(clayBandsOffsetNoise->getValue(worldX * 1.0f / 512.0f, worldZ * 1.0f / 512.0f) * 2.0f);
	return clayBands[(y + offset + 64) % 64];
}



void MesaBiome::Decorator::decorateOres(BlockSource* source, Random& random, const BlockPos& origin) {
	BiomeDecorator::decorateOres(source, random, origin);

	//additional gold
	decorateDepthSpan(source, random, origin, 20, goldOreFeature, 32, 80);
}

} // namespace vgen

