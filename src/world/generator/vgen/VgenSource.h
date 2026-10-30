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
// CurseStone | vgen — VgenSource: the vanilla 1.0.0 overworld generator

#pragma once
#include "world/generator/Generator.h"
#include "world/generator/NeighborChunkIO.h"
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

class VgenSource : public Generator, public vgen::ITerrainFeed, public ISpillQueue {
public:
        explicit VgenSource(int64_t seed);
        ~VgenSource() override;

        const char* name() const override { return "vgen_normal"; }

        void generate(GenerationContext& ctx) override;
        void populate(GenerationContext& ctx) override;

        // Vanilla-like spawn: surface at the world origin area.
        mcp_vector3_t getSpawn() const override { return mcp_v3(127.5, 128.0, 127.5); }

        // Wire the live-chunk sink (implemented by ChunkManager). With it,
        // neighbor-aimed writes reach chunks that are already generated or
        // loaded from disk directly; without it they can only wait in the
        // spill queue for a future populate, which cuts features in half
        // whenever the neighbor came first.
        void setNeighborSink(INeighborBlockSink* sink) { mNeighborSink = sink; }

        // ITerrainFeed (the 3x3 virtual-neighborhood backing store)
        vgen::LevelChunk* acquireTerrain(int cx, int cz) override;
        void commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) override;
        void applySpill(vgen::LevelChunk& center, int cx, int cz) override;
        vgen::Level& getLevel() override { return mLevel; }
        vgen::Dimension& getDimension() override { return mDimension; }

        // ISpillQueue (consumed by ChunkManager at chunk install time)
        void queueSpill(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) override;
        void drainSpills(int cx, int cz, mcp_chunk_t* chunk) override;

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

        INeighborBlockSink* mNeighborSink = nullptr;

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
