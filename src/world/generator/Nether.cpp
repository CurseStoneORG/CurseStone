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
#include "world/generator/Nether.h"
#include "world/generator/vgen/Blocks.h"
#include "world/format/Chunk.h"
#include "world/format/ChunkSection.h"
#include <string.h>

namespace cursestone::world::generator {

using vgen::Block;
using vgen::BlockPos;
using vgen::BlockVolume;
using vgen::ChunkPos;
using vgen::LevelChunk;

// Construction — vanilla HellRandomLevelSource ctor (noise init order
// matters: every noise ctor draws from the same RNG sequentially).
static int ensureHellBiomes() {
	static bool done = false;
	if (!done) {
		vgen::Block::initBlocks();
		vgen::Material::initMaterials();
		vgen::Biome::initBiomes();
		done = true;
	}
	return 0;
}

Nether::Nether(int64_t seed)
	: mSeed(seed),
	  mLevel((ensureHellBiomes(), (vgen::RandomSeed)(uint64_t)seed)),
	  mBiomeSource((vgen::RandomSeed)(uint64_t)seed, 8 /* Hell */),
	  mDimension(mLevel, &mBiomeSource),
	  mSource(mLevel, mDimension, (vgen::RandomSeed)(uint64_t)seed) {

	// The hell dimension keeps its lava sea 32 blocks deep.
	mDimension._setSeaLevel(32);
}

Nether::~Nether() {
	std::lock_guard<std::mutex> l(mCacheMutex);
	mTerrainCache.clear(); // shared_ptr frees chunks with no other users
}

// Terrain chunk construction (vanilla loadChunk)
void Nether::generateTerrainInto(LevelChunk& lc, vgen::HellRandomLevelSource::ThreadData& td) {
	mSource.loadChunk(lc, false);
}

// ITerrainFeed — terrain cache + spill bookkeeping
std::shared_ptr<vgen::LevelChunk> Nether::acquireTerrain(int cx, int cz) {
	const int64_t key = ngenKey(cx, cz);
	{
		std::lock_guard<std::mutex> l(mCacheMutex);
		auto it = mTerrainCache.find(key);
		if (it != mTerrainCache.end()) {
			it->second.stamp = ++mCacheStamp;
			return it->second.chunk;
		}
	}

	// build outside the lock (noise work is heavy)
	auto& td = vgen::HellRandomLevelSource::threadData();
	auto lc = std::make_shared<vgen::LevelChunk>(mLevel, mDimension, ChunkPos(cx, cz));
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
			mTerrainCache.erase(oldest); // freed only with the last user
		}
	} else {
		lc.reset(); // lost a race — another worker cached it first
		e.stamp = ++mCacheStamp;
		return e.chunk;
	}
	return e.chunk;
}

void Nether::commitNeighborWrite(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) {
	if (mNeighborSink) {
		mNeighborSink->commitNeighborWrite(cx, cz, index, id, data);
		return;
	}
	// No live-chunk sink wired (offline tools): fall back to the plain
	// spill queue, which still serves later populations.
	queueSpill(cx, cz, index, id, data);
}

void Nether::queueSpill(int cx, int cz, uint32_t index, uint8_t id, uint8_t data) {
	std::lock_guard<std::mutex> l(mSpillMutex);
	mSpill[ngenKey(cx, cz)].push_back({index, id, data});
}

void Nether::drainSpills(int cx, int cz, mcp_chunk_t* chunk) {
	std::vector<SpillEntry> pending;
	{
		std::lock_guard<std::mutex> l(mSpillMutex);
		auto it = mSpill.find(ngenKey(cx, cz));
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

void Nether::applySpill(vgen::LevelChunk& center, int cx, int cz) {
	std::vector<SpillEntry> pending;
	{
		std::lock_guard<std::mutex> l(mSpillMutex);
		auto it = mSpill.find(ngenKey(cx, cz));
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

// Chunk sink plumbing
// True when every block id of one 16-high section of the virtual volume
// is air. The virtual layout is y-contiguous per column (stride 128), so
// a section is 256 runs of 16 bytes.
static bool hellSectionAllAir(const LevelChunk& lc, int sec) {
	const int y0 = sec * 16;
	for (int col = 0; col < 256; ++col) {
		const vgen::BlockID* run = lc.mBlocks + y0 + col * 128;
		for (int y = 0; y < 16; ++y) {
			if (run[y].value != 0)
				return false;
		}
	}
	return true;
}

void Nether::copyVirtualToSink(const LevelChunk& src, mcp_chunk_t* dst) {
	// 0.15 worlds are 128 blocks tall: only sections 0..7 map onto the
	// virtual volume, and reading past them would alias the next column.
	for (int sec = 0; sec < 128 / 16; ++sec) {
		mcp_chunk_section_t* s = dst->sections[sec];
		if (!s && hellSectionAllAir(src, sec)) {
			// All air in the virtual volume and nothing allocated in the
			// sink: writing would only allocate the section to fill it
			// with the zeros it already reads as. Skip the alloc + writes.
			continue;
		}
		s = mcp_chunk_get_or_alloc_section(dst, (uint8_t)sec);
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
		dst->biomes[i] = (uint8_t)(b ? b->mId : 8);
	}
}

void Nether::copySinkToVirtual(const mcp_chunk_t* src, LevelChunk& dst) {
	memset(dst.mBlocks, 0, sizeof(dst.mBlocks));
	memset(dst.mData, 0, sizeof(dst.mData));
	// Mirror of copyVirtualToSink: 0.15 chunks are 128 blocks tall, so
	// only sections 0..7 map onto the virtual 128-high volume.
	for (int sec = 0; sec < 128 / 16; ++sec) {
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
			vgen::Biome* b = vgen::Biome::getBiome((int)src->biomes[i]);
			dst.mBiomes[i] = b;
		}
	} else {
		vgen::Biome* def = vgen::Biome::getBiome(8);
		for (int i = 0; i < 256; ++i)
			dst.mBiomes[i] = def;
	}
	dst.recalcHeightmap();
}

// Generator interface
void Nether::generate(GenerationContext& ctx) {
	mcp_chunk_t* sink = ctx.chunk;
	if (!sink)
		return;

	std::shared_ptr<vgen::LevelChunk> tc = acquireTerrain(ctx.chunk_x, ctx.chunk_z);
	copyVirtualToSink(*tc, sink);
}

void Nether::populate(GenerationContext& ctx) {
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

	// fortresses, lava springs, fire, glowstone, mushrooms, quartz ore
	mSource.postProcess(source, center);

	// write the populated center back into the sink chunk
	copyVirtualToSink(center, sink);
}

} // namespace cursestone::world::generator
