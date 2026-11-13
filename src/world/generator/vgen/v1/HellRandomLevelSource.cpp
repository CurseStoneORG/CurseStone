/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
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
#include "vgen/v1/HellRandomLevelSource.h"

#include "vgen/VgenWorld.h"
#include "vgen/Blocks.h"
#include "vgen/feature/FlowerFeature.h"
#include "vgen/feature/HellFireFeature.h"
#include "vgen/feature/HellSpringFeature.h"
#include "vgen/feature/GlowStoneFeature.h"
#include "vgen/feature/OreFeature.h"
#include "vgen/material/Material.h"

namespace vgen {

HellRandomLevelSource::ThreadData& HellRandomLevelSource::threadData() {
	static thread_local ThreadData td;
	return td;
}

HellRandomLevelSource::HellRandomLevelSource(Level& level, Dimension& dimension, RandomSeed seed) :
	mLevel(level)
	, mDimension(dimension) {

	Random random(seed);

	lperlinNoise1 = make_unique<PerlinNoise>(random, 16);
	lperlinNoise2 = make_unique<PerlinNoise>(random, 16);
	perlinNoise1 = make_unique<PerlinNoise>(random, 8);
	perlinNoise2 = make_unique<PerlinNoise>(random, 4);
	perlinNoise3 = make_unique<PerlinNoise>(random, 4);

	scaleNoise = make_unique<PerlinNoise>(random, 10);
	depthNoise = make_unique<PerlinNoise>(random, 16);
}

void HellRandomLevelSource::prepareHeights(BlockVolume& blocks, int xOffs, int zOffs) {

	float noiseBuffer[HellGen::NOISE_SIZE_X * HellGen::NOISE_SIZE_Y * HellGen::NOISE_SIZE_Z];

	getHeights(noiseBuffer, xOffs * HellGen::CHUNKCOUNT_XZ, 0, zOffs * HellGen::CHUNKCOUNT_XZ);

	auto levelHeightBits = (int)Math::log2((float)blocks.getHeight());
	auto step = 1 << levelHeightBits;

	for (uint32_t xc = 0; xc < HellGen::CHUNKCOUNT_XZ; xc++) {
		for (uint32_t zc = 0; zc < HellGen::CHUNKCOUNT_XZ; zc++) {
			for (uint32_t yc = 0; yc < blocks.getHeight() / HellGen::CHUNK_HEIGHT; yc++) {
				float yStep = 1 / (float)HellGen::CHUNK_HEIGHT;
				float s0 = noiseBuffer[((xc + 0) * HellGen::NOISE_SIZE_Z + (zc + 0)) * HellGen::NOISE_SIZE_Y + (yc + 0)];
				float s1 = noiseBuffer[((xc + 0) * HellGen::NOISE_SIZE_Z + (zc + 1)) * HellGen::NOISE_SIZE_Y + (yc + 0)];
				float s2 = noiseBuffer[((xc + 1) * HellGen::NOISE_SIZE_Z + (zc + 0)) * HellGen::NOISE_SIZE_Y + (yc + 0)];
				float s3 = noiseBuffer[((xc + 1) * HellGen::NOISE_SIZE_Z + (zc + 1)) * HellGen::NOISE_SIZE_Y + (yc + 0)];

				float s0a = (noiseBuffer[((xc + 0) * HellGen::NOISE_SIZE_Z + (zc + 0)) * HellGen::NOISE_SIZE_Y + (yc + 1)] - s0) * yStep;
				float s1a = (noiseBuffer[((xc + 0) * HellGen::NOISE_SIZE_Z + (zc + 1)) * HellGen::NOISE_SIZE_Y + (yc + 1)] - s1) * yStep;
				float s2a = (noiseBuffer[((xc + 1) * HellGen::NOISE_SIZE_Z + (zc + 0)) * HellGen::NOISE_SIZE_Y + (yc + 1)] - s2) * yStep;
				float s3a = (noiseBuffer[((xc + 1) * HellGen::NOISE_SIZE_Z + (zc + 1)) * HellGen::NOISE_SIZE_Y + (yc + 1)] - s3) * yStep;

				for (uint32_t y = 0; y < HellGen::CHUNK_HEIGHT; y++) {
					float xStep = 1 / (float)HellGen::CHUNK_WIDTH;

					float _s0 = s0;
					float _s1 = s1;
					float _s0a = (s2 - s0) * xStep;
					float _s1a = (s3 - s1) * xStep;

					for (uint32_t x = 0; x < HellGen::CHUNK_WIDTH; x++) {
						int offs = (x + xc * HellGen::CHUNK_WIDTH) << (levelHeightBits + 4) | (0 + zc * HellGen::CHUNK_WIDTH) << levelHeightBits | (yc * HellGen::CHUNK_HEIGHT + y);
						float zStep = 1 / (float)HellGen::CHUNK_WIDTH;

						float val = _s0;
						float vala = (_s1 - _s0) * zStep;

						for (uint32_t z = 0; z < HellGen::CHUNK_WIDTH; z++) {
							BlockID blockId(BlockID::AIR);
							if (yc * HellGen::CHUNK_HEIGHT + y < (uint32_t)mDimension.getSeaLevel()) {
								blockId = Block::mStillLava->mID;
							}
							if (val > 0) {
								blockId = Block::mNetherrack->mID;
							}

							blocks.setBlock(offs, blockId);
							offs += step;
							val += vala;
						}
						_s0 += _s0a;
						_s1 += _s1a;
					}

					s0 += s0a;
					s1 += s1a;
					s2 += s2a;
					s3 += s3a;
				}
			}
		}
	}
}

void HellRandomLevelSource::buildSurfaces(BlockVolume& blocks, const ChunkPos& chunkPos, Random& random) {

	int waterHeight = blocks.getHeight() - 64;

	Vec3 noisePos = Vec3((float)chunkPos.x, 0.f, (float)chunkPos.z) * 16.0f;

	float sandBuffer[16 * 16];
	perlinNoise2->getRegion(sandBuffer, Vec3(noisePos.x, noisePos.z, 0.0f), 16, 16, 1, Vec3(1.0f, 1.0f, 1.0f));
	float gravelBuffer[16 * 16];
	perlinNoise2->getRegion(gravelBuffer, Vec3(noisePos.x, 109.f, noisePos.z), 16, 1, 16, Vec3(1.0f, 1.0f, 1.0f));
	float depthBuffer[16 * 16];
	perlinNoise3->getRegion(depthBuffer, Vec3(noisePos.x, noisePos.z, 0.0f), 16, 16, 1, Vec3(2.0f, 2.0f, 2.0f));

	for (int x = 0; x < 16; x++) {
		for (int z = 0; z < 16; z++) {
			bool sand = (sandBuffer[x + z * 16] + random.nextFloat() * 0.2f) > 0;
			bool gravel = (gravelBuffer[x + z * 16] + random.nextFloat() * 0.2f) > 0;
			int runDepth = (int)(depthBuffer[x + z * 16] / 3 + 3 + random.nextFloat() * 0.25f);

			int run = -1;

			BlockID top = Block::mNetherrack->mID;
			BlockID material = Block::mNetherrack->mID;

			for (int y = (blocks.getHeight() - 1); y >= 0; y--) {
				auto offs = blocks.index(Pos{ x, y, z });

				uint32_t randUpperHeight = (blocks.getHeight() - 1) - random.nextInt(5);
				int randLowerHeight = random.nextInt(5);
				if ((uint32_t)y >= randUpperHeight || y <= randLowerHeight) {
					blocks.setBlock(offs, Block::mBedrock->mID);
				}
				else {
					const BlockID oldBlock(blocks.block(offs));
					if (oldBlock == 0) {
						run = -1;
					}
					else if (oldBlock == Block::mNetherrack->mID) {
						if (run == -1) {
							if (runDepth <= 0) {
								top = BlockID::AIR;
								material = Block::mNetherrack->mID;
							}
							else if (y >= waterHeight - 4 && y <= waterHeight + 1) {
								top = Block::mNetherrack->mID;
								material = Block::mNetherrack->mID;
								if (gravel) {
									top = Block::mGravel->mID;
									material = Block::mNetherrack->mID;
								}

								if (sand) {
									top = Block::mSoulSand->mID;
									material = Block::mSoulSand->mID;
								}
							}

							if (y < waterHeight && (top == BlockID::AIR || Block::mBlocks[top]->getMaterial().isType(MaterialType::Air))) {
								top = Block::mStillLava->mID;
							}

							run = runDepth;
							if (y >= waterHeight - 1) {
								blocks.setBlock(offs, top);
							}
							else {
								blocks.setBlock(offs, material);
							}
						}
						else if (run > 0) {
							run--;
							blocks.setBlock(offs, material);
						}
					}
				}
			}
		}
	}
}

void HellRandomLevelSource::loadChunk(LevelChunk& levelChunk, bool forceImmediateReplacementDataLoad) {
	UNUSED_PARAMETER(forceImmediateReplacementDataLoad);

	auto& p = levelChunk.getPosition();
	auto& td = threadData();

	// Terrain builds are CONTEXT-FREE (see VgenSource::generateTerrainInto):
	// a local Random seeded like the vanilla per-chunk stream keeps the
	// produced bytes identical whether the chunk builds from its own
	// generate() task or lazily inside a neighbour's populate, and the
	// caller's populate RNG stays untouched mid-sequence.
	Random terrainRandom((RandomSeed)(int64_t)(p.x * 341873128712ll + p.z * 132897987541ll));

	memset(td.blockBuffer.data(), 0, sizeof(td.blockBuffer));

	BlockVolume box{ buffer_span_mut<uint8_t>(td.blockBuffer.data(), td.blockBuffer.data() + td.blockBuffer.size()),
			CHUNK_WIDTH, HellGen::GEN_DEPTH, CHUNK_WIDTH };

	mDimension.getBiomes().fillBiomeData(&levelChunk, levelChunk.getMin().x, levelChunk.getMin().z);

	prepareHeights(box, p.x, p.z);
	buildSurfaces(box, p, terrainRandom);
	td.hellCaveFeature.apply(box, levelChunk, mDimension, terrainRandom);

	levelChunk.setAllBlockIDs(box.blocks.begin(), HellGen::GEN_DEPTH);

	levelChunk.recalcHeightmap();
}

void HellRandomLevelSource::getHeights(float* noiseBuffer, int x, int y, int z) {

	float s = 1 * 684.412f;
	float hs = 1 * 684.412f * 3;

	const int noiseSize2d = HellGen::NOISE_SIZE_X * HellGen::NOISE_SIZE_Y;
	Vec3 noisePos((float)x, (float)y, (float)z);

	float scaleRegion[HellGen::NOISE_SIZE_X * HellGen::NOISE_SIZE_Y];
	scaleNoise->getRegion(scaleRegion, noisePos, HellGen::NOISE_SIZE_X, 1, HellGen::NOISE_SIZE_Z, Vec3(1.0f, 0, 1.0f));
	float depthRegion[HellGen::NOISE_SIZE_X * HellGen::NOISE_SIZE_Y];
	depthNoise->getRegion(depthRegion, noisePos, HellGen::NOISE_SIZE_X, 1, HellGen::NOISE_SIZE_Z, Vec3(100.0f, 0, 100.0f));

	const int noiseSize3d = HellGen::NOISE_SIZE_X * HellGen::NOISE_SIZE_Y * HellGen::NOISE_SIZE_Z;

	float noiseRegionPrimary[HellGen::NOISE_SIZE_X * HellGen::NOISE_SIZE_Y * HellGen::NOISE_SIZE_Z];
	perlinNoise1->getRegion(noiseRegionPrimary, noisePos, HellGen::NOISE_SIZE_X, HellGen::NOISE_SIZE_Y, HellGen::NOISE_SIZE_Z, Vec3(s / 80.0f, hs / 60.0f, s / 80.0f));
	float noiseRegionA[HellGen::NOISE_SIZE_X * HellGen::NOISE_SIZE_Y * HellGen::NOISE_SIZE_Z];
	lperlinNoise1->getRegion(noiseRegionA, noisePos, HellGen::NOISE_SIZE_X, HellGen::NOISE_SIZE_Y, HellGen::NOISE_SIZE_Z, Vec3(s, hs, s));
	float noiseRegionB[HellGen::NOISE_SIZE_X * HellGen::NOISE_SIZE_Y * HellGen::NOISE_SIZE_Z];
	lperlinNoise2->getRegion(noiseRegionB, noisePos, HellGen::NOISE_SIZE_X, HellGen::NOISE_SIZE_Y, HellGen::NOISE_SIZE_Z, Vec3(s, hs, s));

	int p = 0;
	float yoffs[HellGen::NOISE_SIZE_Y];

	for (int yy = 0; yy < HellGen::NOISE_SIZE_Y; yy++) {
		yoffs[yy] = Math::cos(yy * Math::PI * 6 / (float)HellGen::NOISE_SIZE_Y) * 2;

		float dd = (float)yy;
		if (yy > HellGen::NOISE_SIZE_Y / 2) {
			dd = (float)(HellGen::NOISE_SIZE_Y - 1) - yy;
		}
		if (dd < 4) {
			dd = 4 - dd;
			yoffs[yy] -= dd * dd * dd * 10;
		}
	}

	for (int xx = 0; xx < HellGen::NOISE_SIZE_X; xx++) {

		for (int zz = 0; zz < HellGen::NOISE_SIZE_Z; zz++) {

			float floating = 0;

			for (int yy = 0; yy < HellGen::NOISE_SIZE_Y; yy++) {
				float val = 0;

				float yOffs = yoffs[yy];

				float bb = noiseRegionA[p] / 512;
				float cc = noiseRegionB[p] / 512;

				float v = (noiseRegionPrimary[p] / 10 + 1) / 2;
				if (v < 0) {
					val = bb;
				}
				else if (v > 1) {
					val = cc;
				}
				else {
					val = bb + (cc - bb) * v;
				}
				val -= yOffs;

				if (yy > HellGen::NOISE_SIZE_Y - 4) {
					float slide = (yy - (HellGen::NOISE_SIZE_Y - 4)) / (4 - 1.0f);
					val = val * (1 - slide) + -10 * slide;
				}

				if (yy < floating) {
					float slide = (floating - yy) / (4);
					if (slide < 0) {
						slide = 0;
					}
					if (slide > 1) {
						slide = 1;
					}
					val = val * (1 - slide) + -10 * slide;
				}

				noiseBuffer[p] = val;
				p++;
			}
		}
	}
}

bool HellRandomLevelSource::postProcess(BlockSource& source, LevelChunk& lc) {

	auto& lcPosition = lc.getPosition();
	auto& td = threadData();
	auto& random = td.random;

	const int xo = lcPosition.x * 16;
	const int zo = lcPosition.z * 16;
	BlockPos origin(xo, 0, zo);

	netherBridgeFeature.apply(lc, mDimension, random);

	// restore seed after calls to "apply" and before any calls to "postProcess"
	random.setSeed((RandomSeed)(uint64_t)mLevel.getSeed());
	int xScale = random.nextInt() / 2 * 2 + 1;
	int zScale = random.nextInt() / 2 * 2 + 1;
	random.setSeed((RandomSeed)(uint64_t)(((lcPosition.x * xScale) + (lcPosition.z * zScale)) ^ mLevel.getSeed()));

	netherBridgeFeature.postProcess(&source, random, lcPosition.x, lcPosition.z);

	// Springs ship SETTLED still lava (same contract as the overworld
	// SpringFeature): a flowing id is a DYNAMIC cell that wakes on chunk
	// install, and 8 springs + 16 traps per chunk turned every walk
	// through the nether into a settle cascade — one UpdateBlockPacket
	// per cell (a client re-tessellation each) plus a relight, the same
	// FPS tank the water generator fixed. Still lava renders identically
	// and only re-enters the simulation when a neighbour block changes,
	// so a player digging into a trap gets the real vanilla flow.
	HellSpringFeature hellSpringFeature(Block::mStillLava->mID, false);

	for (int i = 0; i < 8; i++) {
		int x = random.nextInt(16) + 8;
		int y = random.nextInt(HellGen::GEN_DEPTH - 8) + 4;
		int z = random.nextInt(16) + 8;
		BlockPos featurePlacement(origin.offset(x, y, z));
		hellSpringFeature.place(source, featurePlacement, random);
	}

	HellFireFeature hellFireFeature;
	int randc = random.nextInt(10) + 1;
	int count = random.nextInt(randc) + 1;

	for (int i = 0; i < count; i++) {
		int x = random.nextInt(16) + 8;
		int y = random.nextInt(HellGen::GEN_DEPTH - 8) + 4;
		int z = random.nextInt(16) + 8;
		BlockPos featurePlacement(origin.offset(x, y, z));
		hellFireFeature.place(source, featurePlacement, random);
	}

	GlowStoneFeature glowStoneFeature;
	randc = random.nextInt(10) + 1;
	count = random.nextInt(randc);
	for (int i = 0; i < count; i++) {
		int x = random.nextInt(16) + 8;
		int y = random.nextInt(HellGen::GEN_DEPTH - 8) + 4;
		int z = random.nextInt(16) + 8;
		BlockPos featurePlacement(origin.offset(x, y, z));
		glowStoneFeature.place(source, featurePlacement, random);
	}
	for (int i = 0; i < 10; i++) {
		int x = random.nextInt(16) + 8;
		int y = random.nextInt(HellGen::GEN_DEPTH);
		int z = random.nextInt(16) + 8;
		BlockPos featurePlacement(origin.offset(x, y, z));
		glowStoneFeature.place(source, featurePlacement, random);
	}

	FlowerFeature brownMushroomFeature(Block::mBrownMushroom->mID);
	if (random.nextInt(1) == 0) {
		int x = random.nextInt(16) + 8;
		int y = random.nextInt(HellGen::GEN_DEPTH);
		int z = random.nextInt(16) + 8;
		BlockPos featurePlacement(origin.offset(x, y, z));
		brownMushroomFeature.place(source, featurePlacement, random);
	}

	FlowerFeature redMushroomFeature(Block::mRedMushroom->mID);
	if (random.nextInt(1) == 0) {
		int x = random.nextInt(16) + 8;
		int y = random.nextInt(HellGen::GEN_DEPTH);
		int z = random.nextInt(16) + 8;
		BlockPos featurePlacement(origin.offset(x, y, z));
		redMushroomFeature.place(source, featurePlacement, random);
	}

	OreFeature quartzFeature(Block::mQuartzOre->mID, 14);

	for (int i = 0; i < 16; i++) {
		int x = random.nextInt(16);
		int y = random.nextInt(HellGen::GEN_DEPTH - 20) + 10;
		int z = random.nextInt(16);
		BlockPos featurePlacement(origin.offset(x, y, z));
		quartzFeature.place(source, featurePlacement, random);
	}

	// Traps ship settled too — same reason as the springs above.
	HellSpringFeature lavaTrapFeature(Block::mStillLava->mID, true);

	for (int i = 0; i < 16; i++) {
		int x = random.nextInt(16);
		int y = random.nextInt(HellGen::GEN_DEPTH - 20) + 10;
		int z = random.nextInt(16);
		BlockPos featurePlacement(origin.offset(x, y, z));
		lavaTrapFeature.place(source, featurePlacement, random);
	}

	lc.recalcHeightmap();

	return true;
}

} // namespace vgen
