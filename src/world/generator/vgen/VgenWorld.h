// CurseStone | vgen — world-surface shim (LevelChunk / Level / Dimension /
// BlockSource) over a 3x3 virtual neighborhood.
//
// The vanilla population step (RandomLevelSource::postProcess) runs against
// a 3x3 chunk neighborhood reached through a BlockSource. CurseStone
// generates and populates one chunk at a time on AsyncPool workers, so
// this shim materializes the neighborhood virtually:
//
//   * the CENTER chunk is a working copy of the real sink chunk;
//   * the 8 neighbors are deterministic terrain-only chunks pulled from
//     VgenSource's terrain cache (generated on demand — the vanilla
//     terrain pipeline is seed-deterministic per chunk);
//   * every write aimed at a NON-center chunk is also recorded as a
//     "spill" on the source; when that neighbor populates later it
//     replays the spill before running its own features, which gives the
//     exact vanilla cross-chunk feature behavior (trees spanning chunk
//     borders, lake edges, structure pieces) without the engine ever
//     having to hold 9 real chunks.
//
// Heightmap semantics stay vanilla: mHeightmap = topmost light-blocking
// block, getTopRainBlockPos = first y whose material blocks motion or is
// liquid, plus one.
#pragma once
#include "vgen/Vanilla.h"
#include "vgen/Blocks.h"
#include "vgen/Random.h"
#include "vgen/Direction.h"
#include "vgen/Facing.h"
#include "vgen/biome/BiomeSource.h"
#include "vgen/material/Material.h"
#include <stdint.h>

namespace vgen {

class LevelChunk;
class Level;
class Dimension;
class BlockSource;
class VgenSource;
class Biome;
class BlockEntity { public: virtual ~BlockEntity() = default; };
class Entity;
class Player;

// opaque stubs — the generator never touches real entities.
// Spell these as `class` (like every upstream forward declaration does):
// MSVC mangles class and struct differently (V vs U), so a struct spelling
// here splits the symbol set between translation units that saw the shim
// first and ones that saw an upstream header first (LNK2019 on the feature
// constructors).
class Entity {
public:
        virtual ~Entity() = default;
};
class Player : public Entity {};

// ---------------------------------------------------------------------------
// Terrain feed — implemented by VgenSource.
// ---------------------------------------------------------------------------
class ITerrainFeed {
public:
        virtual ~ITerrainFeed() = default;
        // Terrain-only chunk for (cx,cz); may come from the cache or be
        // generated on demand. The pointer stays owned by the feed.
        virtual LevelChunk* acquireTerrain(int cx, int cz) = 0;
        // Deliver a write aimed outside the center chunk: applied to the
        // live chunk when one exists, queued for that chunk's populate /
        // install otherwise. Keeps cross-chunk features (trees, lakes,
        // structure pieces) whole no matter what order chunks generate in.
        virtual void commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) = 0;
        // Replay every spill recorded for (cx,cz) into `center`.
        virtual void applySpill(LevelChunk& center, int cx, int cz) = 0;
        virtual Level& getLevel() = 0;
        virtual Dimension& getDimension() = 0;
};

// ---------------------------------------------------------------------------
// LevelChunk — virtual 16x128x16 chunk in BlockVolume layout
// ([x][z][y]: index = y + (z + x*16) * 128).
// ---------------------------------------------------------------------------
class LevelChunk {
public:
        LevelChunk(Level& level, Dimension& dimension, const ChunkPos& pos);

        static const int kWidth = 16;
        static const int kHeight = 128;
        static const int kVolume = 16 * 128 * 16;

        BlockID mBlocks[kVolume];
        NibblePair mData[kVolume / 2];
        Biome* mBiomes[256];
        int mHeightmap[256];
        int mRainHeights[256];
        ChunkPos mPos;
        Level* mLevel = nullptr;
        Dimension* mDimension = nullptr;

        const ChunkPos& getPosition() const { return mPos; }
        BlockPos getMin() const { return BlockPos(mPos.x * 16, 0, mPos.z * 16); }
        BlockPos getMax() const { return BlockPos(mPos.x * 16 + 15, 127, mPos.z * 16 + 15); }
        Level& getLevel() const { return *mLevel; }
        Dimension& getDimension() const { return *mDimension; }

        int getAllocatedHeight() const { return kHeight; }

        Biome* getBiome(const ChunkBlockPos& p) const {
                return mBiomes[p.index2D()];
        }
        void setBiome(Biome& biome, const ChunkBlockPos& p) {
                mBiomes[p.index2D()] = &biome;
        }

        BlockID getBlock(const ChunkBlockPos& p) const {
                return mBlocks[(uint32_t)p.y + ((uint32_t)p.z + (uint32_t)p.x * 16) * 128];
        }
        DataID getData(const ChunkBlockPos& p) const {
                uint32_t idx = (uint32_t)p.y + ((uint32_t)p.z + (uint32_t)p.x * 16) * 128;
                const NibblePair& pair = mData[idx / 2];
                return idx % 2 == 0 ? (DataID)pair.first : (DataID)pair.second;
        }
        void setBlock(const ChunkBlockPos& p, BlockID id) {
                uint32_t idx = (uint32_t)p.y + ((uint32_t)p.z + (uint32_t)p.x * 16) * 128;
                mBlocks[idx] = id;
                _touchColumn(p.x, p.z);
        }
        void setData(const ChunkBlockPos& p, DataID v) {
                uint32_t idx = (uint32_t)p.y + ((uint32_t)p.z + (uint32_t)p.x * 16) * 128;
                NibblePair& pair = mData[idx / 2];
                if (idx % 2 == 0)
                        pair.first = (uint8_t)v;
                else
                        pair.second = (uint8_t)v;
        }

        // RandomLevelSource::loadChunk shovels the whole BlockVolume in one go;
        // both use the same [x][z][y] layout, so these are memcpys.
        void setAllBlockIDs(const BlockID* src, int height);
        void setAllBlockData(const NibblePair* src, int height);

        void recalcHeightmap();
        void _touchColumn(int x, int z);
        const BlockPos getTopRainBlockPos(const ChunkBlockPos& pos);
        Height getAboveTopSolidBlock(const ChunkBlockPos& start, bool dimensionShowsSky,
                                     bool includeWater, bool includeLeaves) const;
        // vanilla defers a light update for lava placed during carving;
        // the engine recomputes light after generation, so this is a no-op
        void deferLightEmitter(const BlockPos& pos) { (void)pos; }
};

// ---------------------------------------------------------------------------
// Level / Dimension
// ---------------------------------------------------------------------------
class Level {
public:
        Level(RandomSeed seed) : mSeed(seed) {}
        RandomSeed getSeed() const { return mSeed; }
        Dimension& getDimension() { return *mDimension; }
        void _setDimension(Dimension* d) { mDimension = d; }

private:
        RandomSeed mSeed;
        Dimension* mDimension = nullptr;
};

struct LightLayer {
        int get(int, int, int) const { return 15; }
};

class Dimension {
public:
        Dimension(Level& level, BiomeSource* biomes)
            : mLevel(level), mBiomes(biomes) {}
        const LightLayer& getSkyLightLayer() const { return mSkyLight; }
        int getSpawnYPosition() const { return mSeaLevel; }
        BiomeSource& getBiomes() const { return *mBiomes; }
        Height getSeaLevel() const { return mSeaLevel; }
        Level& getLevel() const { return mLevel; }
        void _setSeaLevel(Height h) { mSeaLevel = h; }

private:
        Level& mLevel;
        BiomeSource* mBiomes = nullptr;
        Height mSeaLevel = 63;
        LightLayer mSkyLight;
};

// ---------------------------------------------------------------------------
// BlockSource — 3x3 virtual neighborhood read/write surface.
// ---------------------------------------------------------------------------
class BlockSource {
public:
        BlockSource(ITerrainFeed& feed, LevelChunk& center);
        ~BlockSource();

        Level& getLevel() const;
        Dimension& getDimension() const;
        BiomeSource& getBiomes() const;
        int getMaxHeight() const { return 128; }
        int getSeaLevel() const { return 63; }

        Biome& getBiome(const BlockPos& pos) const;

        BlockID getBlockID(const BlockPos& pos) const;
        BlockID getBlockID(int x, int y, int z) const;
        DataID getData(const BlockPos& pos) const;
        const Block& getBlock(const BlockPos& pos) const;
        FullBlock getBlockAndData(const BlockPos& pos) const;
        const Material& getMaterial(const BlockPos& pos) const;
        const Material& getMaterial(int x, int y, int z) const {
                return getMaterial(BlockPos(x, y, z));
        }
        bool isEmptyBlock(const BlockPos& pos) const;
        bool isEmptyBlock(int x, int y, int z) const {
                return isEmptyBlock(BlockPos(x, y, z));
        }
        bool isSolidBlockingBlock(const BlockPos& pos) const;
        int getBrightness(const BlockPos& pos) const;
        int getBrightness(const LightLayer& layer, const BlockPos& pos) const {
                (void)layer;
                return getBrightness(pos);
        }
        int getHeightmap(const BlockPos& pos) const;
        BlockSource& getRegion(const BlockPos& pos) const { return const_cast<BlockSource&>(*this); }

        bool setBlockAndData(const BlockPos& pos, BlockID id, int flags,
                             Entity* placer = nullptr);
        bool setBlockAndData(const BlockPos& pos, BlockID id, DataID data, int flags,
                             Entity* placer = nullptr);
        // No default on flags (matches upstream BlockSource.h): a 5-argument
        // call like setBlockAndData(x, y, z, id, FLAGS) must resolve to the
        // (x, y, z, FullBlock, flags) overload — the upstream meaning — not
        // silently turn the flag into block data. The default made both
        // overloads viable for that shape and clang/MSVC (C2666) reject the
        // resulting similar-conversions tie.
        bool setBlockAndData(int x, int y, int z, BlockID id, DataID data,
                             int flags) {
                return setBlockAndData(BlockPos(x, y, z), id, data, flags);
        }
        bool setBlockAndData(const BlockPos& pos, const FullBlock& block, int flags,
                             Entity* placer = nullptr);
        bool setBlockAndData(int x, int y, int z, const FullBlock& block, int flags) {
                return setBlockAndData(BlockPos(x, y, z), block, flags);
        }
        void removeBlock(const BlockPos& pos);
        void removeBlock(int x, int y, int z) { removeBlock(BlockPos(x, y, z)); }

        BlockSource* getExistingChunk(const ChunkPos& cp) { return this; } // shim
        const BlockPos getTopRainBlockPos(const BlockPos& pos) const;
        bool shouldFreezeIgnoreNeighbors(const BlockPos& pos) const;
        bool shouldFreeze(const BlockPos& pos, bool checkNeighbors) const;
        LevelChunk* getChunkAt(const BlockPos& pos) const;
        BlockSource* getBlockSource(const BlockPos&) { return this; }

        Height getAboveTopSolidBlock(const BlockPos& pos, bool includeWater = false,
                                     bool includeLeaves = false);
        Height getAboveTopSolidBlock(int x, int z, bool includeWater = false,
                                     bool includeLeaves = false);
        BlockPos getHeightmapPos(const BlockPos& xzPos) const;
        Height getHeightmap(int x, int z) const;
        // StructurePiece queries tile entities during generation; the shim has none.
        bool hasBlock(const BlockPos& pos) const {
                return !isEmptyBlock(pos);
        }
        BlockEntity* getBlockEntity(const BlockPos& pos) const {
                (void)pos;
                return nullptr;
        }

        LevelChunk& centerChunk() const { return mCenter; }

private:
        LevelChunk* _chunkAt(int cx, int cz) const;
        LevelChunk* _chunkAtBlock(int x, int z) const;
        bool _writeBlock(LevelChunk& c, const ChunkBlockPos& p, BlockID id, DataID data,
                         bool recordSpill);

        ITerrainFeed& mFeed;
        LevelChunk& mCenter;
        LevelChunk* mNeighbors[3][3] = {}; // [dx+1][dz+1]; center is mCenter
};

} // namespace vgen
