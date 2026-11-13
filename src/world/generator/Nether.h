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
#include "world/generator/Generator.h"
#include "world/generator/NeighborChunkIO.h"
#include "vgen/VgenWorld.h"
#include "vgen/biome/Biome.h"
#include "vgen/biome/BiomeSource.h"
#include "vgen/biome/FixedBiomeSource.h"
#include "vgen/v1/HellRandomLevelSource.h"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cursestone::world::generator {

// Nether — the vanilla hell generator wired onto the Generator interface.
// The terrain half (netherrack density field, lava sea to sea level 32,
// bedrock shell, hell caves) builds terrain-only chunks into a cache; the
// feature half (fortresses, lava springs, fire, glowstone, mushrooms,
// quartz ore) populates over a virtual 3x3 neighborhood exactly like the
// overworld VgenSource does.
class Nether : public Generator, public vgen::ITerrainFeed, public ISpillQueue {
public:
	explicit Nether(int64_t seed);
	~Nether() override;

	const char* name() const override {
		return "nether";
	}
	void generate(GenerationContext& ctx) override;
	void populate(GenerationContext& ctx) override;

	// Mirrors the era reference Nether.getSpawn(): (128, 64, 128)
	mcp_vector3_t getSpawn() const override {
		return mcp_v3(128.0, 64.0, 128.0);
	}

	// Wire the live-chunk sink (implemented by ChunkManager) so feature
	// writes that cross a chunk border reach chunks that are already
	// generated or loaded from disk directly.
	void setNeighborSink(INeighborBlockSink* sink) { mNeighborSink = sink; }

	// ITerrainFeed (the 3x3 virtual-neighborhood backing store)
	std::shared_ptr<vgen::LevelChunk> acquireTerrain(int cx, int cz) override;
	void commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) override;
	void applySpill(vgen::LevelChunk& center, int cx, int cz) override;
	vgen::Level& getLevel() override { return mLevel; }
	vgen::Dimension& getDimension() override { return mDimension; }

	// ISpillQueue (consumed by ChunkManager at chunk install time)
	void queueSpill(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) override;
	void drainSpills(int cx, int cz, mcp_chunk_t* chunk) override;

private:
	void generateTerrainInto(vgen::LevelChunk& lc, vgen::HellRandomLevelSource::ThreadData& td);
	void copyVirtualToSink(const vgen::LevelChunk& src, mcp_chunk_t* dst);
	void copySinkToVirtual(const mcp_chunk_t* src, vgen::LevelChunk& dst);

	INeighborBlockSink* mNeighborSink = nullptr;

	int64_t mSeed; // as int64; vanilla truncates to uint32 when seeding
	vgen::Level mLevel;
	vgen::FixedBiomeSource mBiomeSource;
	vgen::Dimension mDimension;
	vgen::HellRandomLevelSource mSource;

	// terrain cache — terrain-only, immutable after fill (same ownership
	// rules as the overworld cache: shared_ptr so a worker copying a
	// neighbor snapshot never races an eviction).
	struct CacheEntry {
		std::shared_ptr<vgen::LevelChunk> chunk;
		uint64_t stamp = 0;
	};
	static const size_t kTerrainCacheLimit = 512;
	std::mutex mCacheMutex;
	std::unordered_map<int64_t, CacheEntry> mTerrainCache;
	uint64_t mCacheStamp = 0;

	// feature spill: writes aimed at chunks that are not the current center
	struct SpillEntry {
		uint32_t index;
		uint8_t id;
		uint8_t data;
	};
	std::mutex mSpillMutex;
	std::unordered_map<int64_t, std::vector<SpillEntry>> mSpill;
};

static inline int64_t ngenKey(int cx, int cz) {
	return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;
}

} // namespace cursestone::world::generator
