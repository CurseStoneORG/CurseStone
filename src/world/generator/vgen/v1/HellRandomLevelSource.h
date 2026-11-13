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
#pragma once

#include "vgen/Vanilla.h"
#include "vgen/v1/LargeHellCaveFeature.h"
#include "vgen/synth/PerlinNoise.h"
#include "vgen/structure/NetherBridgeFeature.h"

namespace vgen {

namespace HellGen {
	static const int GEN_DEPTH = 128;
	static const int CHUNK_HEIGHT = 8;
	static const int CHUNK_WIDTH = 4;
	static const int CHUNKCOUNT_XZ = 16 / CHUNK_WIDTH;
	static const int NOISE_SIZE_X = CHUNKCOUNT_XZ + 1;
	static const int NOISE_SIZE_Y = GEN_DEPTH / CHUNK_HEIGHT + 1;
	static const int NOISE_SIZE_Z = CHUNKCOUNT_XZ + 1;
}

class BlockSource;
class LevelChunk;

class HellRandomLevelSource {
public:

	struct ThreadData {
		Random random;
		LargeHellCaveFeature hellCaveFeature;
		std::array<uint8_t, HellGen::GEN_DEPTH * CHUNK_COLUMNS + (HellGen::GEN_DEPTH * CHUNK_COLUMNS) / 2> blockBuffer;
	};

	HellRandomLevelSource(Level& level, Dimension& dimension, RandomSeed seed);

	// Terrain half of the vanilla loadChunk: biomes, heights, surfaces,
	// caves, heightmap. Runs once per terrain-only chunk.
	void loadChunk(LevelChunk& levelChunk, bool forceImmediateReplacementDataLoad);

	// Feature half of the vanilla postProcess, over the virtual 3x3
	// neighborhood (BlockSource) instead of a live ChunkViewSource.
	bool postProcess(BlockSource& source, LevelChunk& lc);

	void prepareHeights(BlockVolume& blocks, int xOffs, int zOffs);
	void buildSurfaces(BlockVolume& blocks, const ChunkPos& chunkPos, Random& random);

	static ThreadData& threadData();

private:

	void getHeights(float* noiseBuffer, int x, int y, int z);

private:

	Level& mLevel;
	Dimension& mDimension;

	Unique<PerlinNoise> lperlinNoise1;
	Unique<PerlinNoise> lperlinNoise2;
	Unique<PerlinNoise> perlinNoise1;
	Unique<PerlinNoise> perlinNoise2;
	Unique<PerlinNoise> perlinNoise3;
	Unique<PerlinNoise> scaleNoise;
	Unique<PerlinNoise> depthNoise;

	NetherBridgeFeature netherBridgeFeature;
};

} // namespace vgen
