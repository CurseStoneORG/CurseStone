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
// CurseStone | vgen — VgenSource implementation.

#include "world/generator/vgen/VgenSource.h"
#include "world/generator/vgen/Blocks.h"
#include "world/generator/vgen/material/Material.h"
#include "world/generator/vgen/feature/LakeFeature.h"
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"
#include <float.h>
#include <math.h>
#include <string.h>

namespace cursestone::world::generator {

using vgen::Block;
using vgen::BlockPos;
using vgen::BlockVolume;
using vgen::ChunkPos;
using vgen::LevelChunk;

// ---------------------------------------------------------------------------
// Construction — vanilla RandomLevelSource ctor (noise init order matters:
// every noise ctor draws from the same RNG sequentially).
// ---------------------------------------------------------------------------
static int ensureBiomes() {
        static bool done = false;
        if (!done) {
                vgen::Block::initBlocks();
                vgen::Material::initMaterials();
                vgen::Biome::initBiomes();
                done = true;
        }
        return 0;
}

VgenSource::VgenSource(int64_t seed)
    : mSeed(seed),
      mLevel((ensureBiomes(), (vgen::RandomSeed)(uint64_t)seed)),
      mBiomeSource((vgen::RandomSeed)(uint64_t)seed, vgen::GeneratorType::Overworld),
      mDimension(mLevel, &mBiomeSource) {

        // Seed-dependent biome state (mesa clay band layout).
        vgen::Biome::refreshBiomes((vgen::RandomSeed)(uint64_t)seed);

        vgen::Random random((vgen::RandomSeed)(uint64_t)seed);
        minLimitPerlinNoise.reset(new vgen::PerlinNoise(random, 16));
        maxLimitPerlinNoise.reset(new vgen::PerlinNoise(random, 16));
        mainPerlinNoise.reset(new vgen::PerlinNoise(random, 8));
        surfaceNoise.reset(new vgen::PerlinSimplexNoise(random, 4));
        scaleNoise.reset(new vgen::PerlinNoise(random, 10));
        depthNoise.reset(new vgen::PerlinNoise(random, 16));
        forestNoise.reset(new vgen::PerlinNoise(random, 8));

        for (auto xb : vgen::range_incl(-2, 2)) {
                for (auto zb : vgen::range_incl(-2, 2)) {
                        float weight = 10.0f / vgen::Math::sqrt(xb * xb + zb * zb + 0.2f);
                        biomeWeights[xb + 2 + (zb + 2) * 5] = weight;
                }
        }
}

VgenSource::~VgenSource() {
        std::lock_guard<std::mutex> l(mCacheMutex);
        for (auto& kv : mTerrainCache)
                delete kv.second.chunk;
        mTerrainCache.clear();
}

// ---------------------------------------------------------------------------
// Per-worker scratch
// ---------------------------------------------------------------------------
VgenSource::ThreadData& VgenSource::threadData() {
        static thread_local ThreadData td;
        return td;
}

// ---------------------------------------------------------------------------
// Terrain pipeline — verbatim from RandomLevelSource.cpp
// ---------------------------------------------------------------------------
namespace Gen {
        static const int CHUNK_HEIGHT = 8;
        static const int CHUNK_WIDTH = 4;
        static const int CHUNKCOUNT_XZ = 16 / CHUNK_WIDTH;
        static const int CHUNKCOUNT_Y = vgen::LEVEL_GEN_HEIGHT / CHUNK_HEIGHT;
        static const int NOISE_SIZE_X = CHUNKCOUNT_XZ + 1;
        static const int NOISE_SIZE_Y = vgen::LEVEL_GEN_HEIGHT / CHUNK_HEIGHT + 1;
        static const int NOISE_SIZE_Z = CHUNKCOUNT_XZ + 1;
} // namespace Gen

void VgenSource::getHeights(float* noiseBuffer, vgen::Biome** biomes, int x, int y, int z) {
        const float coordinateScale = 1 * 684.412f;
        const float heightScale = 1 * 684.412f;
        const float upperLimitScale = 512.0f;
        const float lowerLimitScale = 512.0f * 0.5f;

        const int noiseSize2d = Gen::NOISE_SIZE_X * Gen::NOISE_SIZE_Y;
        vgen::Vec2 noisePos2D((float)x, (float)z);

        float* depthRegion = (float*)alloca(sizeof(float) * noiseSize2d);
        depthNoise->getRegion(depthRegion, noisePos2D, Gen::NOISE_SIZE_X, Gen::NOISE_SIZE_Z, vgen::Vec2(200.0, 200.0), 0.5);

        const int noiseSize3d = Gen::NOISE_SIZE_X * Gen::NOISE_SIZE_Y * Gen::NOISE_SIZE_Z;
        vgen::Vec3 noisePos((float)x, (float)y, (float)z);

        float* mainNoiseRegion = (float*)alloca(sizeof(float) * noiseSize3d);
        mainPerlinNoise->getRegion(mainNoiseRegion, noisePos, Gen::NOISE_SIZE_X, Gen::NOISE_SIZE_Y, Gen::NOISE_SIZE_Z,
                                   vgen::Vec3(coordinateScale / 80.0f, heightScale / 160.0f, coordinateScale / 80.0f));

        float* minLimitRegion = (float*)alloca(sizeof(float) * noiseSize3d);
        minLimitPerlinNoise->getRegion(minLimitRegion, noisePos, Gen::NOISE_SIZE_X, Gen::NOISE_SIZE_Y, Gen::NOISE_SIZE_Z,
                                       vgen::Vec3(coordinateScale, heightScale * 1.25f, coordinateScale));

        float* maxLimitRegion = (float*)alloca(sizeof(float) * noiseSize3d);
        maxLimitPerlinNoise->getRegion(maxLimitRegion, noisePos, Gen::NOISE_SIZE_X, Gen::NOISE_SIZE_Y, Gen::NOISE_SIZE_Z,
                                       vgen::Vec3(coordinateScale, heightScale, coordinateScale));

        x = z = 0;

        int noiseBufIndex = 0;
        int pp = 0;

        const float baseSize = 17.0f / 2.0f;

        for (auto xx : vgen::range(Gen::NOISE_SIZE_X)) {
                for (auto zz : vgen::range(Gen::NOISE_SIZE_Z)) {
                        float scaleSum = 0;
                        float depthSum = 0;
                        float weightSum = 0;

                        int rr = 1;

                        // [RH] Perform a 5x5 approximately-gaussian smoothing kernel in order to make biome
                        //      height transitions less abrupt
                        vgen::Biome* middleBiome = biomes[(xx + 2) + (zz + 2) * (Gen::NOISE_SIZE_X + 5)];

                        for (auto xb : vgen::range_incl(-rr, rr)) {
                                for (auto zb : vgen::range_incl(-rr, rr)) {
                                        vgen::Biome* b = biomes[(xx + xb + 2) + (zz + zb + 2) * (Gen::NOISE_SIZE_X + 5)];
                                        float depth = b->mDepth;
                                        float scale = b->mScale;
                                        // We should never have amplified biomes in Pocket Edition
                                        float weight = biomeWeights[xb + 2 + (zb + 2) * 5] / (depth + 2);
                                        if (b->mDepth > middleBiome->mDepth) {
                                                weight /= 2;
                                        }
                                        scaleSum += scale * weight;
                                        depthSum += depth * weight;
                                        weightSum += weight;
                                }
                        }
                        scaleSum /= weightSum;
                        depthSum /= weightSum;

                        scaleSum = scaleSum * 0.9f + 0.1f;
                        depthSum = (depthSum * 4 - 1) / 8.0f;

                        float rdepth = (depthRegion[pp] / 8000.0f);
                        if (rdepth < 0) {
                                rdepth = -rdepth * 0.3f;
                        }
                        rdepth = rdepth * 3.0f - 2.0f;

                        if (rdepth < 0) {
                                rdepth /= 2;
                                if (rdepth < -1) {
                                        rdepth = -1;
                                }
                                rdepth /= 1.4f;
                                rdepth /= 2;
                        } else {
                                if (rdepth > 1) {
                                        rdepth = 1;
                                }
                                rdepth /= 8;
                        }

                        pp++;

                        float depth = depthSum;
                        float scale = scaleSum;
                        depth += rdepth * 0.2f;
                        depth = depth * baseSize / 8.0f;

                        float yCenter = baseSize + depth * 4;

                        for (auto yy : vgen::range(Gen::NOISE_SIZE_Y)) {
                                float yOffs = ((yy - (yCenter)) * 12 * 128 / vgen::LEVEL_GEN_HEIGHT) / scale;

                                if (yOffs < 0) {
                                        yOffs *= 4;
                                }

                                float blendMin = minLimitRegion[noiseBufIndex] / lowerLimitScale;
                                float blendMax = maxLimitRegion[noiseBufIndex] / upperLimitScale;

                                float blendFactor = (mainNoiseRegion[noiseBufIndex] / 10 + 1) / 2;
                                float val = vgen::Math::clampedLerp(blendMin, blendMax, blendFactor) - yOffs;

                                // [RH] If we're in the 4 noise segments, decrease 'val' somewhat sharply in order
                                //      to ensure that there aren't truncated mountain peaks at the top of the terrain.
                                if (yy > Gen::NOISE_SIZE_Y - 4) {
                                        float slide = (yy - (Gen::NOISE_SIZE_Y - 4)) / (4 - 1.0f);
                                        val = val * (1 - slide) + -10 * slide;
                                }

                                noiseBuffer[noiseBufIndex] = val;
                                noiseBufIndex++;
                        }
                }
        }
}

void VgenSource::prepareHeights(BlockVolume& blocks, int xOffs, int zOffs, ThreadData& td) {
        static const int kNoiseBufLen = Gen::NOISE_SIZE_X * Gen::NOISE_SIZE_Y * Gen::NOISE_SIZE_Z;
        static const int kBiomeCount = 10 * 10;

        float noiseBuffer[kNoiseBufLen];
        vgen::Biome* biomes[kBiomeCount];

        mDimension.getBiomes().fillRawBiomeData(biomes, xOffs * Gen::CHUNK_WIDTH - 2, zOffs * Gen::CHUNK_WIDTH - 2);
        getHeights(noiseBuffer, biomes, xOffs * Gen::CHUNKCOUNT_XZ, 0, zOffs * Gen::CHUNKCOUNT_XZ);

        auto levelHeightBits = (int)vgen::Math::log2((float)blocks.getHeight());

        for (auto xc : vgen::range(Gen::CHUNKCOUNT_XZ)) {
                int xCurr = xc * Gen::NOISE_SIZE_Z;
                int xNext = (xc + 1) * Gen::NOISE_SIZE_Z;

                for (auto zc : vgen::range(Gen::CHUNKCOUNT_XZ)) {
                        int xzMinMin = (xCurr + zc) * Gen::NOISE_SIZE_Y;
                        int xzMinMax = (xCurr + zc + 1) * Gen::NOISE_SIZE_Y;
                        int xzMaxMin = (xNext + zc) * Gen::NOISE_SIZE_Y;
                        int xzMaxMax = (xNext + zc + 1) * Gen::NOISE_SIZE_Y;

                        for (auto yc : vgen::range(Gen::CHUNKCOUNT_Y)) {
                                float yStep = 1 / (float)Gen::CHUNK_HEIGHT;
                                float s0 = noiseBuffer[xzMinMin + yc];
                                float s1 = noiseBuffer[xzMinMax + yc];
                                float s2 = noiseBuffer[xzMaxMin + yc];
                                float s3 = noiseBuffer[xzMaxMax + yc];

                                float s0a = (noiseBuffer[xzMinMin + yc + 1] - s0) * yStep;
                                float s1a = (noiseBuffer[xzMinMax + yc + 1] - s1) * yStep;
                                float s2a = (noiseBuffer[xzMaxMin + yc + 1] - s2) * yStep;
                                float s3a = (noiseBuffer[xzMaxMax + yc + 1] - s3) * yStep;

                                for (auto y : vgen::range(Gen::CHUNK_HEIGHT)) {
                                        float xStep = 1 / (float)Gen::CHUNK_WIDTH;

                                        float _s0 = s0;
                                        float _s1 = s1;
                                        float _s0a = (s2 - s0) * xStep;
                                        float _s1a = (s3 - s1) * xStep;

                                        for (auto x : vgen::range(Gen::CHUNK_WIDTH)) {
                                                int offs = ((x + xc * Gen::CHUNK_WIDTH) << (levelHeightBits + 4)) |
                                                           ((0 + zc * Gen::CHUNK_WIDTH) << levelHeightBits) |
                                                           (yc * Gen::CHUNK_HEIGHT + y);
                                                offs -= blocks.getHeight();
                                                float zStep = 1 / (float)Gen::CHUNK_WIDTH;

                                                float val = _s0;
                                                float vala = (_s1 - _s0) * zStep;
                                                val -= vala;

                                                for (auto i : vgen::range(Gen::CHUNK_WIDTH)) {
                                                        UNUSED_VARIABLE(i);
                                                        if ((val += vala) > 0) {
                                                                blocks.setBlock(offs += blocks.getHeight(), Block::mStone->mID);
                                                        } else if (yc * Gen::CHUNK_HEIGHT + y < mDimension.getSeaLevel()) {
                                                                blocks.setBlock(offs += blocks.getHeight(), Block::mStillWater->mID);
                                                        } else {
                                                                blocks.setBlock(offs += blocks.getHeight(), vgen::BlockID::AIR);
                                                        }
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

void VgenSource::buildSurfaces(ThreadData& td, BlockVolume& blocks,
                               LevelChunk& levelChunk, const ChunkPos& chunkPos) {
        // Vanilla reaches the noise origin through the implicit
        // BlockPos(const ChunkPos&) conversion, which scales by the world
        // chunk width of 16 blocks (LevelConstants.h). The local
        // Gen::CHUNK_WIDTH of 4 only widens the prepareHeights interpolation
        // lattice and must not leak here: sampling at *4 slid the surface
        // window 12 blocks into every neighbor, so each surface patch
        // contour repeated chunk after chunk, sliding and shrinking at the
        // borders. Sample at the chunk's world-block origin so the 16x16
        // windows tile the world with no overlap, like vanilla.
        vgen::Vec3 chunkBlockPos((float)(chunkPos.x * LevelChunk::kWidth), 0.f,
                                 (float)(chunkPos.z * LevelChunk::kWidth));
        surfaceNoise->getRegion(td.depthBuffer, vgen::Vec2(chunkBlockPos.x, chunkBlockPos.z), 16, 16,
                                vgen::Vec2(1.f / 8, 1.f / 8), 1.0f);

        vgen::ChunkBlockPos p;
        for (p.x = 0; p.x < 16; ++p.x) {
                for (p.z = 0; p.z < 16; ++p.z) {
                        BlockPos t = p + levelChunk.getMin();
                        levelChunk.getBiome(p)->buildSurfaceAt(
                            td.random, blocks, t, td.depthBuffer[p.index2D()], mDimension.getSeaLevel());
                }
        }
}

// ---------------------------------------------------------------------------
// Terrain chunk construction (vanilla loadChunk terrain half)
// ---------------------------------------------------------------------------
void VgenSource::generateTerrainInto(LevelChunk& lc, ThreadData& td) {
        const ChunkPos& p = lc.getPosition();

        td.random.setSeed((vgen::RandomSeed)(int64_t)(p.x * 341872712l + p.z * 132899541l)); //@fix

        memset(td.blockBuffer.data(), 0, sizeof(td.blockBuffer));

        BlockVolume box{vgen::buffer_span_mut<uint8_t>(td.blockBuffer.data(),
                                                       td.blockBuffer.data() + td.blockBuffer.size()),
                        16, vgen::LEVEL_GEN_HEIGHT, 16};

        mDimension.getBiomes().fillBiomeData(&lc, lc.getMin().x, lc.getMin().z);
        prepareHeights(box, p.x, p.z, td); //biomes, temperatures);
        buildSurfaces(td, box, lc, p);

        td.caveFeature.apply(box, lc, mDimension, td.random);

        lc.setAllBlockIDs(box.blocks.begin(), box.getHeight());
        lc.setAllBlockData(box.data.begin(), box.getHeight());
        lc.recalcHeightmap();
}

// ---------------------------------------------------------------------------
// ITerrainFeed — terrain cache + spill bookkeeping
// ---------------------------------------------------------------------------
vgen::LevelChunk* VgenSource::acquireTerrain(int cx, int cz) {
        const int64_t key = vgenKey(cx, cz);
        {
                std::lock_guard<std::mutex> l(mCacheMutex);
                auto it = mTerrainCache.find(key);
                if (it != mTerrainCache.end()) {
                        it->second.stamp = ++mCacheStamp;
                        return it->second.chunk;
                }
        }

        // build outside the lock (noise work is heavy)
        auto& td = threadData();
        vgen::LevelChunk* lc = new vgen::LevelChunk(mLevel, mDimension, ChunkPos(cx, cz));
        generateTerrainInto(*lc, td);

        std::lock_guard<std::mutex> l(mCacheMutex);
        auto& e = mTerrainCache[key];
        if (e.chunk == nullptr) {
                e.chunk = lc;
                // stamp FIRST: eviction picks the oldest entry, and a fresh
                // entry with stamp 0 would evict itself (and hand back a
                // dangling pointer) the moment the cache overflows
                e.stamp = ++mCacheStamp;
                // evict the oldest entries when the cache is full
                while (mTerrainCache.size() > kTerrainCacheLimit) {
                        auto oldest = mTerrainCache.begin();
                        for (auto it = mTerrainCache.begin(); it != mTerrainCache.end(); ++it) {
                                if (it->second.stamp < oldest->second.stamp)
                                        oldest = it;
                        }
                        delete oldest->second.chunk;
                        mTerrainCache.erase(oldest);
                }
        } else {
                delete lc; // lost a race — another worker cached it first
                e.stamp = ++mCacheStamp;
                return e.chunk;
        }
        return e.chunk;
}

void VgenSource::commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) {
        if (mNeighborSink) {
                mNeighborSink->commitNeighborWrite(cx, cz, index, id, data);
                return;
        }
        // No live-chunk sink wired (offline tools): fall back to the plain
        // spill queue, which still serves later populations.
        queueSpill(cx, cz, index, id, data);
}

void VgenSource::queueSpill(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) {
        std::lock_guard<std::mutex> l(mSpillMutex);
        mSpill[vgenKey(cx, cz)].push_back({index, id, data});
}

void VgenSource::drainSpills(int cx, int cz, mcp_chunk_t* chunk) {
        std::vector<SpillEntry> pending;
        {
                std::lock_guard<std::mutex> l(mSpillMutex);
                auto it = mSpill.find(vgenKey(cx, cz));
                if (it == mSpill.end())
                        return;
                pending = std::move(it->second);
                mSpill.erase(it);
        }
        if (!chunk)
                return;
        for (const SpillEntry& e : pending) {
                uint32_t y = e.index % 128;
                uint32_t rest = e.index / 128;
                uint32_t z = rest % 16;
                uint32_t x = rest / 16;
                mcp_chunk_set_block_full(chunk, (int)x, (int)y, (int)z, e.id, e.data);
        }
}

void VgenSource::applySpill(vgen::LevelChunk& center, int cx, int cz) {
        std::vector<SpillEntry> pending;
        {
                std::lock_guard<std::mutex> l(mSpillMutex);
                auto it = mSpill.find(vgenKey(cx, cz));
                if (it == mSpill.end())
                        return;
                pending = std::move(it->second);
                mSpill.erase(it);
        }
        for (const SpillEntry& e : pending) {
                uint32_t y = e.index % 128;
                uint32_t rest = e.index / 128;
                uint32_t z = rest % 16;
                uint32_t x = rest / 16;
                vgen::ChunkBlockPos p((uint8_t)x, (vgen::Height)y, (uint8_t)z);
                center.setBlock(p, vgen::BlockID(e.id));
                center.setData(p, e.data);
        }
}

// ---------------------------------------------------------------------------
// Chunk sink plumbing
// ---------------------------------------------------------------------------
void VgenSource::copyVirtualToSink(const LevelChunk& src, mcp_chunk_t* dst) {
        // 0.15 worlds are LEVEL_GEN_HEIGHT (128) blocks tall: the vanilla
        // generator only fills y 0..127, and the virtual LevelChunk is a
        // 16x128x16 volume (stride 128). Copying sections 8..15 would read
        // past each column into the next one's data (aliasing garbage).
        for (int sec = 0; sec < vgen::LEVEL_GEN_HEIGHT / 16; ++sec) {
                mcp_chunk_section_t* s = mcp_chunk_get_or_alloc_section(dst, (uint8_t)sec);
                if (!s)
                        continue;
                for (int yl = 0; yl < 16; ++yl) {
                        int yAbs = sec * 16 + yl;
                        for (int z = 0; z < 16; ++z) {
                                for (int x = 0; x < 16; ++x) {
                                        uint32_t vidx = (uint32_t)yAbs + ((uint32_t)z + (uint32_t)x * 16) * 128;
                                        uint32_t sidx = mcp_section_block_index(x, yl, z);
                                        s->blocks[sidx] = (uint8_t)src.mBlocks[vidx].value;
                                        uint8_t v = 0;
                                        const vgen::NibblePair& np = src.mData[vidx / 2];
                                        v = (vidx % 2 == 0) ? (uint8_t)np.first : (uint8_t)np.second;
                                        uint32_t nidx = mcp_section_nibble_index(x, yl, z);
                                        uint8_t* arr = s->data;
                                        if (!arr)
                                                continue;
                                        if (nidx & 1)
                                                arr[nidx >> 1] = (uint8_t)((arr[nidx >> 1] & 0x0F) | ((v & 0x0F) << 4));
                                        else
                                                arr[nidx >> 1] = (uint8_t)((arr[nidx >> 1] & 0xF0) | (v & 0x0F));
                                }
                        }
                }
        }
        mcp_chunk_recompute_section_flags(dst);
        if (!dst->biomes)
                dst->biomes = (uint8_t*)malloc(256);
        for (int i = 0; i < 256; ++i) {
                vgen::Biome* b = src.mBiomes[i];
                dst->biomes[i] = (uint8_t)(b ? b->mId : 1);
        }
}

void VgenSource::copySinkToVirtual(const mcp_chunk_t* src, LevelChunk& dst) {
        memset(dst.mBlocks, 0, sizeof(dst.mBlocks));
        memset(dst.mData, 0, sizeof(dst.mData));
        // Mirror of copyVirtualToSink: 0.15 chunks are 128 blocks tall, so
        // only sections 0..7 map onto the virtual 128-high volume.
        for (int sec = 0; sec < vgen::LEVEL_GEN_HEIGHT / 16; ++sec) {
                const mcp_chunk_section_t* s = mcp_chunk_get_section(const_cast<mcp_chunk_t*>(src), (uint8_t)sec);
                if (!s || !s->blocks)
                        continue;
                for (int yl = 0; yl < 16; ++yl) {
                        int yAbs = sec * 16 + yl;
                        for (int z = 0; z < 16; ++z) {
                                for (int x = 0; x < 16; ++x) {
                                        uint32_t sidx = mcp_section_block_index(x, yl, z);
                                        uint8_t id = s->blocks[sidx];
                                        uint32_t vidx = (uint32_t)yAbs + ((uint32_t)z + (uint32_t)x * 16) * 128;
                                        dst.mBlocks[vidx] = id;
                                        uint8_t v = 0;
                                        if (s->data) {
                                                uint32_t nidx = mcp_section_nibble_index(x, yl, z);
                                                uint8_t b = s->data[nidx >> 1];
                                                v = (nidx & 1) ? (uint8_t)((b >> 4) & 0x0F) : (uint8_t)(b & 0x0F);
                                        }
                                        vgen::NibblePair& np = dst.mData[vidx / 2];
                                        if (vidx % 2 == 0)
                                                np.first = (uint8_t)(v & 0x0F);
                                        else
                                                np.second = (uint8_t)(v & 0x0F);
                                }
                        }
                }
        }
        if (src->biomes) {
                for (int i = 0; i < 256; ++i) {
                        vgen::Biome* b = vgen::Biome::getBiome((int)src->biomes[i], vgen::Biome::DEFAULT);
                        dst.mBiomes[i] = b;
                }
        } else {
                vgen::Biome* def = vgen::Biome::getBiome(1, vgen::Biome::DEFAULT);
                for (int i = 0; i < 256; ++i)
                        dst.mBiomes[i] = def;
        }
        dst.recalcHeightmap();
}

// ---------------------------------------------------------------------------
// Generator interface
// ---------------------------------------------------------------------------
void VgenSource::generate(GenerationContext& ctx) {
        mcp_chunk_t* sink = ctx.chunk;
        if (!sink)
                return;

        vgen::LevelChunk* tc = acquireTerrain(ctx.chunk_x, ctx.chunk_z);
        copyVirtualToSink(*tc, sink);
}

void VgenSource::populate(GenerationContext& ctx) {
        mcp_chunk_t* sink = ctx.chunk;
        if (!sink)
                return;
        const int cx = ctx.chunk_x;
        const int cz = ctx.chunk_z;

        // center working copy of the real chunk
        LevelChunk center(mLevel, mDimension, ChunkPos(cx, cz));
        copySinkToVirtual(sink, center);

        // replay cross-chunk feature writes recorded by earlier populations
        applySpill(center, cx, cz);

        vgen::BlockSource source(*this, center);

        auto& td = threadData();
        vgen::Random& random = td.random;
        const int xo = cx * 16;
        const int zo = cz * 16;
        BlockPos origin(xo, 0, zo);

        // Ensure that the seed is consistent for lake placement.
        random.setSeed((vgen::RandomSeed)(uint64_t)mSeed);
        int xScale = random.nextInt() / 2 * 2 + 1;
        int zScale = random.nextInt() / 2 * 2 + 1;
        random.setSeed((vgen::RandomSeed)(uint64_t)(((cx * xScale) + (cz * zScale)) ^ mSeed));

        vgen::Biome& biome = source.getBiome(center.getMax());

        // water lakes
        bool hasLake = false;
        if (biome != vgen::Biome::desert && biome != vgen::Biome::desertHills) {
                if (random.nextInt(4) == 0) {
                        int x = random.nextInt(3, 12);
                        int y = random.nextInt(vgen::LEVEL_GEN_HEIGHT);
                        int z = random.nextInt(3, 12);
                        BlockPos featurePlacement = origin.offset(x, y, z);
                        vgen::LakeFeature lakeFeature(Block::mStillWater->mID);
                        lakeFeature.place(source, featurePlacement, random);
                        hasLake = true;
                }
        }

        // lava lakes
        if (!hasLake && random.nextInt(8) == 0) {
                int x = random.nextInt(3, 12);
                int yparam = random.nextInt(vgen::LEVEL_GEN_HEIGHT - 8) + 8;
                int y = random.nextInt(yparam);
                int z = random.nextInt(3, 12);
                if (y >= 60 && (y < 64 || random.nextInt(10) == 0)) {
                        BlockPos featurePlacement = origin.offset(x, y, z);
                        vgen::LakeFeature lakeFeature(Block::mStillLava->mID);
                        lakeFeature.place(source, featurePlacement, random);
                }
        }

        // structures (0.15 set: village, mineshaft, scattered temples)
        mVillageFeature.apply(center, mDimension, random);
        mMineshaftFeature.apply(center, mDimension, random);
        mScatteredFeature.apply(center, mDimension, random);

        random.setSeed((vgen::RandomSeed)(uint64_t)mSeed);
        xScale = random.nextInt() / 2 * 2 + 1;
        zScale = random.nextInt() / 2 * 2 + 1;
        random.setSeed((vgen::RandomSeed)(uint64_t)(((cx * xScale) + (cz * zScale)) ^ mSeed));
        {
                mVillageFeature.postProcess(&source, random, cx, cz);
                mMineshaftFeature.postProcess(&source, random, cx, cz);
                mScatteredFeature.postProcess(&source, random, cx, cz);
        }

        // monster rooms (dungeons)
        {
                for (auto i : vgen::range(8)) {
                        UNUSED_VARIABLE(i);
                        int x = xo + random.nextInt(16) + 8;
                        int y = random.nextInt(vgen::LEVEL_GEN_HEIGHT);
                        int z = zo + random.nextInt(16) + 8;
                        mMonsterRoomFeature.place(source, BlockPos(x, y, z), random);
                }
        }

        // freeze tops (ice on cold-biome water)
        {
                for (auto x : vgen::range(16)) {
                        for (auto z : vgen::range(16)) {
                                const BlockPos topPos = source.getTopRainBlockPos(origin.offset(x, 0, z));
                                const BlockPos belowPos = topPos.below();
                                if (source.shouldFreezeIgnoreNeighbors(belowPos)) {
                                        source.setBlockAndData(belowPos, Block::mIce->mID, Block::UPDATE_CLIENTS);
                                }
                        }
                }
        }

        // biome decoration (trees, grass, flowers, ores, ...)
        {
                random.setSeed((vgen::RandomSeed)(uint64_t)(((cx * xScale) + (cz * zScale)) ^ mSeed));
                biome.decorate(&source, random, center.getMin(), false, FLT_MAX);
        }

        // edge water fix (vanilla _fixWaterAlongEdges)
        fixWaterAlongEdges(center, source);

        // snow cover (0.15 seasons postProcess equivalent)
        snowPass(source, center);

        // write the populated center back into the sink chunk
        copyVirtualToSink(center, sink);
}

// ---------------------------------------------------------------------------
// Population passes
// ---------------------------------------------------------------------------
void VgenSource::fixWaterAlongEdges(LevelChunk& lc, vgen::BlockSource& source) {
        BlockPos min = lc.getMin();
        auto fixColumn = [&](int ox, int oz) {
                BlockPos p = min.offset(ox, 0, oz);
                bool wasAir = true;
                auto h = source.getHeightmap(p);
                for (; p.y < h; ++p.y) {
                        if (source.getBlockID(p) == Block::mStillWater->mID) {
                                if (wasAir) {
                                        source.setBlockAndData(p, Block::mFlowingWater->mID, 0);
                                        wasAir = false;
                                }
                        } else {
                                wasAir = true;
                        }
                }
        };
        for (int z = 0; z < 16; ++z) {
                fixColumn(0, z);
                fixColumn(15, z);
        }
        for (int x = 1; x < 15; ++x) {
                fixColumn(x, 0);
                fixColumn(x, 15);
        }
}

void VgenSource::snowPass(vgen::BlockSource& source, LevelChunk& center) {
        center.recalcHeightmap();

        static vgen::PerlinSimplexNoise snowNoise((vgen::RandomSeed)89328u, 5);
        const float noiseMin = -26.0f;
        const float noiseMax = 26.0f;

        for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                        BlockPos tpos = center.getTopRainBlockPos(vgen::ChunkBlockPos((uint8_t)x, 0, (uint8_t)z));
                        if (tpos.y < 0 || tpos.y > 127)
                                continue;
                        vgen::Biome* b = center.getBiome(vgen::ChunkBlockPos((uint8_t)x, 0, (uint8_t)z));
                        if (!b || !b->canHaveSnowfall())
                                continue;

                        vgen::BlockID at = source.getBlockID(tpos);
                        if (at != vgen::BlockID::AIR)
                                continue; // already snow / occupied
                        vgen::BlockID below = source.getBlockID(tpos.below());
                        if (below == 0 || below == Block::mStillWater->mID || below == Block::mFlowingWater->mID)
                                continue;

                        float val = snowNoise.getValue((vgen::Vec3)(vgen::Vec3((float)tpos.x, (float)tpos.y, (float)tpos.z)));
                        float f = (val - noiseMin) / (noiseMax - noiseMin) - 0.17f;
                        int snowLevel = (int)ceilf(f * (float)b->getMinSnowLayers());
                        if (snowLevel < 1)
                                snowLevel = 1;

                        // neighbor columns higher than this one pile up more snow
                        for (int face = 2; face <= 5; ++face) {
                                BlockPos n = tpos.offset(vgen::Facing::DIRECTION[face].x, 0,
                                                         vgen::Facing::DIRECTION[face].z);
                                int h = source.getTopRainBlockPos(n).y;
                                if (h > tpos.y) {
                                        snowLevel++;
                                } else if (h < tpos.y && snowLevel > 1) {
                                        snowLevel--;
                                }
                        }

                        if (snowLevel > b->getMaxSnowLayers())
                                snowLevel = b->getMaxSnowLayers();
                        if (snowLevel > 8)
                                snowLevel = 8;

                        // 0.15 snow layer: data 0..7 (data 7 = a full block)
                        source.setBlockAndData(tpos, Block::mSnow->mID, (vgen::DataID)(snowLevel - 1), Block::UPDATE_CLIENTS);
                }
        }
}

} // namespace cursestone::world::generator
