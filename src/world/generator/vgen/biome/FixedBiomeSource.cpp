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
#include "vgen/biome/FixedBiomeSource.h"

#include "vgen/VgenWorld.h"

namespace vgen {

FixedBiomeSource::FixedBiomeSource(RandomSeed seed, int biomeId) :
	BiomeSource(seed)
	, biomeId(biomeId) {
}

void FixedBiomeSource::fillBiomeData(LevelChunk* levelChunk, int x, int z) const {
	for (char zOff = 0; zOff < CHUNK_WIDTH; zOff++) {
		for (char xOff = 0; xOff < CHUNK_WIDTH; xOff++) {
			const ChunkBlockPos ctp(xOff, 0, zOff);
			levelChunk->setBiome(*Biome::getBiome(biomeId), ctp);
		}
	}
}

Biome* FixedBiomeSource::getBiome(int blockX, int blockZ) {
	return Biome::getBiome(biomeId);
}

Biome* FixedBiomeSource::getBiome(const BlockPos& block) {
	return Biome::getBiome(biomeId);
}

} // namespace vgen
