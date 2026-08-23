#include "ChunkManager.h"
#include "level/format/LevelProvider.h"
#include "level/format/Chunk.h"
namespace mcppeserver::level {
ChunkManager::ChunkManager() {}
ChunkManager::~ChunkManager() {
    for (auto& kv : chunks_) if (kv.second) mcp_chunk_destroy(kv.second);
}
mcp_chunk_t* ChunkManager::getChunkIfLoaded(int32_t x, int32_t z) const {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = chunks_.find({x,z});
    return it == chunks_.end() ? nullptr : it->second;
}

// Generate a basic flat chunk: bedrock at y=0, dirt y=1..62, grass at y=63
static mcp_chunk_t* generateFlatChunk(int32_t cx, int32_t cz) {
    mcp_chunk_t* c = mcp_chunk_new(cx, cz);
    if (!c) return nullptr;
    c->terrain_populated = true;
    c->light_populated = true;
    c->dirty = false;

    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            // Bedrock at y=0
            mcp_chunk_set_block_id(c, x, 0, z, 7);  // BEDROCK
            // Dirt from y=1 to y=62
            for (int y = 1; y <= 62; ++y) {
                mcp_chunk_set_block_id(c, x, y, z, 3);  // DIRT
            }
            // Grass at y=63
            mcp_chunk_set_block_id(c, x, 63, z, 2);  // GRASS
            // Air above
            // (already 0 by default)
        }
    }

    // Fill biomes with plains (1)
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            mcp_chunk_set_biome(c, x, z, 1);
        }
    }

    // Recalculate height map
    mcp_chunk_recalculate_height_map(c);

    return c;
}

mcp_chunk_t* ChunkManager::getChunk(int32_t x, int32_t z) {
    if (mcp_chunk_t* c = getChunkIfLoaded(x,z)) return c;
    // No provider loading implemented yet — generate a flat chunk instead.
    // This gives the player actual terrain to stand on.
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
