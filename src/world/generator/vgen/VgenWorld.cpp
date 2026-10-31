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
// CurseStone | vgen — world-surface shim implementation.

#include "vgen/VgenWorld.h"
#include "vgen/biome/Biome.h"
#include <string.h>

namespace vgen {

// ---------------------------------------------------------------------------
// LevelChunk
// ---------------------------------------------------------------------------
LevelChunk::LevelChunk(Level& level, Dimension& dimension, const ChunkPos& pos)
    : mPos(pos), mLevel(&level), mDimension(&dimension) {
        memset(mBlocks, 0, sizeof(mBlocks));
        memset(mData, 0, sizeof(mData));
        memset(mBiomes, 0, sizeof(mBiomes));
        memset(mHeightmap, 0, sizeof(mHeightmap));
        for (int i = 0; i < 256; ++i)
                mRainHeights[i] = -999;
}

void LevelChunk::setAllBlockIDs(const BlockID* src, int height) {
        (void)height;
        memcpy(mBlocks, src, sizeof(mBlocks));
        recalcHeightmap();
}

void LevelChunk::setAllBlockData(const NibblePair* src, int height) {
        (void)height;
        memcpy(mData, src, sizeof(mData));
}

void LevelChunk::recalcHeightmap() {
        for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                        int y = kHeight;
                        mRainHeights[x + (z << 4)] = -999;
                        while (y > 0 && Block::mLightBlock[(uint8_t)mBlocks[(uint32_t)(y - 1) + ((uint32_t)z + (uint32_t)x * 16) * 128]] == 0) {
                                --y;
                        }
                        mHeightmap[x + (z << 4)] = y;
                }
        }
}

static bool _isTopSolid(const Material* material, bool includeWater, bool includeLeaves) {
        if (material == nullptr) {
                return false;
        } else if (material->isType(MaterialType::Leaves)) {
                return includeLeaves;
        } else if (includeWater && material->isType(MaterialType::Water)) {
                return true;
        }
        return material->getBlocksMotion();
}

Height LevelChunk::getAboveTopSolidBlock(const ChunkBlockPos& start, bool dimensionShowsSky,
                                         bool includeWater, bool includeLeaves) const {
        const Material* material = nullptr;
        ChunkBlockPos pos = start;

        pos.y = pos.y < kHeight ? pos.y : (Height)kHeight;

        if (!dimensionShowsSky) {
                for (; pos.y > 0; --pos.y) {
                        material = &Block::mBlocks[(uint8_t)mBlocks[(uint32_t)pos.y + ((uint32_t)pos.z + (uint32_t)pos.x * 16) * 128]]->getMaterial();
                        if (!_isTopSolid(material, includeWater, includeLeaves)) {
                                break;
                        }
                }
        }

        while (pos.y >= 0) {
                material = &Block::mBlocks[(uint8_t)mBlocks[(uint32_t)pos.y + ((uint32_t)pos.z + (uint32_t)pos.x * 16) * 128]]->getMaterial();
                if (_isTopSolid(material, includeWater, includeLeaves)) {
                        break;
                }
                --pos.y;
        }

        return pos.y >= 0 ? (Height)(pos.y + 1) : (Height)0;
}

const BlockPos LevelChunk::getTopRainBlockPos(const ChunkBlockPos& pos) {
        const int slot = pos.x | (pos.z << 4);
        if (mRainHeights[slot] == -999) {
                Height y = (Height)getAllocatedHeight();
                int h = -1;
                while (y > 0 && h == -1) {
                        const Material& m = Block::mBlocks[(uint8_t)mBlocks[(uint32_t)y + ((uint32_t)pos.z + (uint32_t)pos.x * 16) * 128]]->getMaterial();
                        if (!m.getBlocksMotion() && !m.isLiquid()) {
                                --y;
                        } else {
                                h = y + 1;
                        }
                }
                mRainHeights[slot] = h < 0 ? 0 : h;
        }
        return BlockPos(mPos.x * 16 + pos.x, mRainHeights[slot], mPos.z * 16 + pos.z);
}

// ---------------------------------------------------------------------------
// BlockSource
// ---------------------------------------------------------------------------
BlockSource::BlockSource(ITerrainFeed& feed, LevelChunk& center)
    : mFeed(feed), mCenter(center) {
        for (int dx = 0; dx < 3; ++dx) {
                for (int dz = 0; dz < 3; ++dz) {
                        if (dx == 1 && dz == 1) {
                                mNeighbors[1][1] = &mCenter;
                                continue;
                        }
                        LevelChunk* cached = mFeed.acquireTerrain(mCenter.mPos.x + dx - 1,
                                                                  mCenter.mPos.z + dz - 1);
                        // Private copy per populate. The cache chunks are
                        // SHARED between threads: writing this populate's
                        // feature blocks straight into them would leak into
                        // another worker's copy-out (terrain freshly copied
                        // to its sink would contain our trees/lakes/ores)
                        // and race the terrain build itself. Each populate
                        // therefore works on its own neighborhood snapshot;
                        // cross-populate propagation goes through the spill
                        // queue / live-chunk sink.
                        mNeighbors[dx][dz] = new LevelChunk(*cached);
                }
        }
}

BlockSource::~BlockSource() {
        for (int dx = 0; dx < 3; ++dx) {
                for (int dz = 0; dz < 3; ++dz) {
                        if (dx == 1 && dz == 1)
                                continue;
                        delete mNeighbors[dx][dz];
                        mNeighbors[dx][dz] = nullptr;
                }
        }
}

Level& BlockSource::getLevel() const { return mFeed.getLevel(); }
Dimension& BlockSource::getDimension() const { return mFeed.getDimension(); }
BiomeSource& BlockSource::getBiomes() const { return mFeed.getDimension().getBiomes(); }

LevelChunk* BlockSource::_chunkAt(int cx, int cz) const {
        int dx = cx - mCenter.mPos.x + 1;
        int dz = cz - mCenter.mPos.z + 1;
        if (dx < 0 || dx > 2 || dz < 0 || dz > 2)
                return nullptr;
        return mNeighbors[dx][dz];
}

LevelChunk* BlockSource::_chunkAtBlock(int x, int z) const {
        return _chunkAt(x >> 4, z >> 4);
}

LevelChunk* BlockSource::getChunkAt(const BlockPos& pos) const {
        return _chunkAtBlock(pos.x, pos.z);
}

Biome& BlockSource::getBiome(const BlockPos& pos) const {
        LevelChunk* c = _chunkAtBlock(pos.x, pos.z);
        if (!c)
                return *Biome::DEFAULT;
        Biome* b = c->getBiome(ChunkBlockPos((uint8_t)(pos.x & 15), (Height)pos.y, (uint8_t)(pos.z & 15)));
        return b ? *b : *Biome::DEFAULT;
}

BlockID BlockSource::getBlockID(const BlockPos& pos) const {
        LevelChunk* c = _chunkAtBlock(pos.x, pos.z);
        if (!c || pos.y < 0 || pos.y >= 128)
                return BlockID(0);
        return c->getBlock(ChunkBlockPos((uint8_t)(pos.x & 15), (Height)pos.y, (uint8_t)(pos.z & 15)));
}

BlockID BlockSource::getBlockID(int x, int y, int z) const {
        return getBlockID(BlockPos(x, y, z));
}

DataID BlockSource::getData(const BlockPos& pos) const {
        LevelChunk* c = _chunkAtBlock(pos.x, pos.z);
        if (!c || pos.y < 0 || pos.y >= 128)
                return 0;
        return c->getData(ChunkBlockPos((uint8_t)(pos.x & 15), (Height)pos.y, (uint8_t)(pos.z & 15)));
}

const Block& BlockSource::getBlock(const BlockPos& pos) const {
        return *Block::mBlocks[getBlockID(pos)];
}

FullBlock BlockSource::getBlockAndData(const BlockPos& pos) const {
        return FullBlock(getBlockID(pos), getData(pos));
}

const Material& BlockSource::getMaterial(const BlockPos& pos) const {
        return Block::mBlocks[getBlockID(pos)]->getMaterial();
}

bool BlockSource::isEmptyBlock(const BlockPos& pos) const {
        return getBlockID(pos) == 0;
}

bool BlockSource::isSolidBlockingBlock(const BlockPos& pos) const {
        const Material& m = getMaterial(pos);
        return m.getBlocksMotion() && !m.isLiquid();
}

int BlockSource::getBrightness(const BlockPos& pos) const {
        // generation-time approximation: full sky above the heightmap, dark below
        LevelChunk* c = _chunkAtBlock(pos.x, pos.z);
        if (!c)
                return 15;
        int h = c->mHeightmap[(pos.x & 15) + ((pos.z & 15) << 4)];
        return pos.y >= h ? 15 : 0;
}

int BlockSource::getHeightmap(const BlockPos& pos) const {
        LevelChunk* c = _chunkAtBlock(pos.x, pos.z);
        if (!c)
                return 0;
        return c->mHeightmap[(pos.x & 15) + ((pos.z & 15) << 4)];
}

bool BlockSource::_writeBlock(LevelChunk& c, const ChunkBlockPos& p, BlockID id,
                              DataID data, bool recordSpill) {
        if (p.y < 0 || p.y >= 128)
                return false;
        c.setBlock(p, id);
        c.setData(p, data);
        if (recordSpill && &c != &mCenter) {
                uint32_t idx = (uint32_t)p.y + ((uint32_t)p.z + (uint32_t)p.x * 16) * 128;
                mFeed.commitNeighborWrite(c.mPos.x, c.mPos.z, idx, (uint8_t)id.value, (uint8_t)data);
        }
        return true;
}

bool BlockSource::setBlockAndData(const BlockPos& pos, BlockID id, int flags,
                                  Entity* placer) {
        return setBlockAndData(pos, id, 0, flags, placer);
}

bool BlockSource::setBlockAndData(const BlockPos& pos, BlockID id, DataID data,
                                  int flags, Entity* placer) {
        (void)flags;
        (void)placer;
        LevelChunk* c = _chunkAtBlock(pos.x, pos.z);
        if (!c)
                return false;
        return _writeBlock(*c,
                           ChunkBlockPos((uint8_t)(pos.x & 15), (Height)pos.y, (uint8_t)(pos.z & 15)),
                           id, data, true);
}

bool BlockSource::setBlockAndData(const BlockPos& pos, const FullBlock& block,
                                  int flags, Entity* placer) {
        return setBlockAndData(pos, block.id, block.data, flags, placer);
}

void BlockSource::removeBlock(const BlockPos& pos) {
        setBlockAndData(pos, BlockID(0), DataID(0), 0, nullptr);
}

const BlockPos BlockSource::getTopRainBlockPos(const BlockPos& pos) const {
        LevelChunk* c = _chunkAtBlock(pos.x, pos.z);
        if (!c)
                return BlockPos(-1, -1, -1);
        return c->getTopRainBlockPos(ChunkBlockPos((uint8_t)(pos.x & 15), (Height)0, (uint8_t)(pos.z & 15)));
}

bool BlockSource::shouldFreeze(const BlockPos& pos, bool checkNeighbors) const {
        const float temp = getBiome(pos).getTemperature();
        if (temp > Biome::RAIN_TEMP_THRESHOLD) {
                return false;
        }
        if (pos.y >= 0 && pos.y < 128) {
                BlockID block = getBlockID(pos);
                if ((block == Block::mStillWater->mID || block == Block::mFlowingWater->mID) &&
                    getData(pos) == 0) {
                        return true;
                }
        }
        (void)checkNeighbors;
        return false;
}

bool BlockSource::shouldFreezeIgnoreNeighbors(const BlockPos& pos) const {
        return shouldFreeze(pos, false);
}

Height BlockSource::getAboveTopSolidBlock(const BlockPos& pos, bool includeWater,
                                          bool includeLeaves) {
        LevelChunk* lc = _chunkAtBlock(pos.x, pos.z);
        if (!lc)
                return (Height)128;
        return lc->getAboveTopSolidBlock(ChunkBlockPos((uint8_t)(pos.x & 15), (Height)pos.y,
                                                       (uint8_t)(pos.z & 15)),
                                         true, includeWater, includeLeaves);
}

Height BlockSource::getAboveTopSolidBlock(int x, int z, bool includeWater, bool includeLeaves) {
        return getAboveTopSolidBlock(BlockPos(x, 127, z), includeWater, includeLeaves);
}

BlockPos BlockSource::getHeightmapPos(const BlockPos& xzPos) const {
        LevelChunk* lc = _chunkAtBlock(xzPos.x, xzPos.z);
        if (!lc)
                return BlockPos(xzPos.x, 0, xzPos.z);
        int h = lc->mHeightmap[(xzPos.x & 15) + ((xzPos.z & 15) << 4)];
        return BlockPos(xzPos.x, h, xzPos.z);
}

Height BlockSource::getHeightmap(int x, int z) const {
        return (Height)getHeightmap(BlockPos(x, 0, z));
}

} // namespace vgen
