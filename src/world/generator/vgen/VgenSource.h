// CurseStone | vgen — VgenSource: the vanilla 1.0.0 overworld generator
// (RandomLevelSource) wired onto CurseStone's Generator interface.
//
// The terrain pipeline is the verbatim port — prepareHeights/getHeights
// run the exact vanilla noise stack (min/max/main Perlin limits, surface
// simplex, scale + depth noise, 5x5 gaussian biome-height smoothing) and
// buildSurfaces uses the per-biome vanilla surface builders, so the
// smoothness and shape of the terrain is the vanilla one. Population
// replays the vanilla postProcess over a 3x3 virtual neighborhood
// (lakes -> village/mineshaft/scattered structures -> dungeons -> ice ->
// biome decoration -> snow -> edge-water fix), with the >0.15 content
// (strongholds, ocean monuments, savanna/snowy villages) cut.
#pragma once
#include "world/generator/Generator.h"
#include "vgen/VgenWorld.h"
#include "vgen/biome/Biome.h"
#include "vgen/biome/BiomeSource.h"
#include "vgen/synth/PerlinNoise.h"
#include "vgen/synth/PerlinSimplexNoise.h"
#include "vgen/v1/LargeCaveFeature.h"
#include "vgen/structure/VillageFeature.h"
#include "vgen/structure/MineshaftFeature.h"
#include "vgen/structure/RandomScatteredLargeFeature.h"
#include "vgen/feature/MonsterRoomFeature.h"
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cursestone::world::generator {

class VgenSource : public Generator, public vgen::ITerrainFeed {
public:
	explicit VgenSource(int64_t seed);
	~VgenSource() override;

	const char* name() const override { return "vgen_normal"; }

	void generate(GenerationContext& ctx) override;
	void populate(GenerationContext& ctx) override;

	// Vanilla-like spawn: surface at the world origin area.
	mcp_vector3_t getSpawn() const override { return mcp_v3(127.5, 128.0, 127.5); }

	// ITerrainFeed (the 3x3 virtual-neighborhood backing store)
	vgen::LevelChunk* acquireTerrain(int cx, int cz) override;
	void recordSpill(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) override;
	void applySpill(vgen::LevelChunk& center, int cx, int cz) override;
	vgen::Level& getLevel() override { return mLevel; }
	vgen::Dimension& getDimension() override { return mDimension; }

private:
	// Per-worker scratch (RandomLevelSource::ThreadData port).
	struct ThreadData {
		float depthBuffer[16 * 16];
		std::array<uint8_t, vgen::LEVEL_GEN_HEIGHT * 256 + vgen::LEVEL_GEN_HEIGHT * 128> blockBuffer;
		vgen::Random random;
		vgen::LargeCaveFeature caveFeature;
	};
	static ThreadData& threadData();

	// RandomLevelSource terrain pipeline (verbatim port).
	void prepareHeights(vgen::BlockVolume& blocks, int xOffs, int zOffs, ThreadData& td);
	void getHeights(float* noiseBuffer, vgen::Biome** biomes, int x, int y, int z);
	void buildSurfaces(ThreadData& td, vgen::BlockVolume& blocks,
	                    vgen::LevelChunk& levelChunk, const vgen::ChunkPos& chunkPos);

	// Terrain-only chunk construction (vanilla loadChunk terrain half).
	void generateTerrainInto(vgen::LevelChunk& lc, ThreadData& td);

	// Chunk sink plumbing.
	void copyVirtualToSink(const vgen::LevelChunk& src, mcp_chunk_t* dst);
	void copySinkToVirtual(const mcp_chunk_t* src, vgen::LevelChunk& dst);

	// Population passes.
	void snowPass(vgen::BlockSource& source, vgen::LevelChunk& center);
	void fixWaterAlongEdges(vgen::LevelChunk& lc, vgen::BlockSource& source);

	int64_t mSeed; // as int64; vanilla truncates to uint32 when seeding
	vgen::Level mLevel;
	vgen::BiomeSource mBiomeSource;
	vgen::Dimension mDimension;

	vgen::Unique<vgen::PerlinNoise> minLimitPerlinNoise;
	vgen::Unique<vgen::PerlinNoise> maxLimitPerlinNoise;
	vgen::Unique<vgen::PerlinNoise> mainPerlinNoise;
	vgen::Unique<vgen::PerlinSimplexNoise> surfaceNoise;
	vgen::Unique<vgen::PerlinNoise> scaleNoise;
	vgen::Unique<vgen::PerlinNoise> depthNoise;
	vgen::Unique<vgen::PerlinNoise> forestNoise;
	float biomeWeights[5 * 5];

	// structures (0.15 set)
	vgen::VillageFeature mVillageFeature;
	vgen::MineshaftFeature mMineshaftFeature;
	vgen::RandomScatteredLargeFeature mScatteredFeature;
	vgen::MonsterRoomFeature mMonsterRoomFeature;

	// terrain cache — terrain-only, immutable after fill
	struct CacheEntry {
		vgen::LevelChunk* chunk = nullptr;
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

static inline int64_t vgenKey(int cx, int cz) {
	return ((int64_t)(uint32_t)cx << 32) | (uint32_t)cz;
}

} // namespace cursestone::world::generator
