#include "ChunkManager.h"
#include "level/format/LevelProvider.h"
#include "level/format/Chunk.h"
#include "level/format/ChunkSection.h"
#include <string.h>
namespace cenisyspe::level {
ChunkManager::ChunkManager() {}
ChunkManager::~ChunkManager() {
    for (auto& kv : chunks_) if (kv.second) mcp_chunk_destroy(kv.second);
}
mcp_chunk_t* ChunkManager::getChunkIfLoaded(int32_t x, int32_t z) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = chunks_.find({x,z});
    return it == chunks_.end() ? nullptr : it->second;
}

// Generate a flat world chunk matching PMMP's flat generator:
//   y=0:  bedrock (7)
//   y=1..2: dirt (3)
//   y=3: grass (2)
//   y=4+: air
// This gives a spawn at y=4 (on top of grass at y=3).
// Light: sky_light = 0xFF (full bright), block_light = 0 (no block light)
static mcp_chunk_t* generateFlatChunk(int32_t cx, int32_t cz) {
    mcp_chunk_t* c = mcp_chunk_new(cx, cz);
    if (!c) return nullptr;
    c->terrain_populated = true;
    c->light_populated = true;
    c->dirty = false;

    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            mcp_chunk_set_block_id(c, x, 0, z, 7);  // BEDROCK
            mcp_chunk_set_block_id(c, x, 1, z, 3);  // DIRT
            mcp_chunk_set_block_id(c, x, 2, z, 3);  // DIRT
            mcp_chunk_set_block_id(c, x, 3, z, 2);  // GRASS
        }
    }

    // Fill sky_light with 0xFF (full bright) for all allocated sections.
    // Without this the world is pitch black (calloc zeroes sky_light).
    for (int s = 0; s < 16; ++s) {
        mcp_chunk_section_t* sec = mcp_chunk_get_or_alloc_section(c, (uint8_t)s);
        if (sec) {
            if (sec->sky_light) memset(sec->sky_light, 0xFF, 2048);
            if (sec->block_light) memset(sec->block_light, 0x00, 2048);
        }
    }

    // Fill biomes with plains (1)
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            mcp_chunk_set_biome(c, x, z, 1);
        }
    }

    mcp_chunk_recalculate_height_map(c);
    return c;
}

mcp_chunk_t* ChunkManager::getChunk(int32_t x, int32_t z) {
    if (mcp_chunk_t* c = getChunkIfLoaded(x,z)) return c;
    mcp_chunk_t* c = generateFlatChunk(x, z);
    if (c) setChunk(x, z, c);
    return c;
}
void ChunkManager::setChunk(int32_t x, int32_t z, mcp_chunk_t* chunk) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto key = ChunkKey{x, z};
    auto it = chunks_.find(key);
    if (it != chunks_.end() && it->second && it->second != chunk)
        mcp_chunk_destroy(it->second);
    chunks_[key] = chunk;
}
void ChunkManager::markDirty(int32_t x, int32_t z) {
    mcp_chunk_t* c = getChunkIfLoaded(x, z);
    if (c) c->dirty = true;
}
int ChunkManager::flushDirty() {
    std::lock_guard<std::mutex> lk(mtx_);
    int n = 0;
    for (auto& kv : chunks_) {
        if (kv.second && kv.second->dirty) {
            kv.second->dirty = false;
            ++n;
        }
    }
    return n;
}
size_t ChunkManager::count() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return chunks_.size();
}
} // namespace
