#include "ChunkManager.h"
#include "level/format/LevelProvider.h"
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
mcp_chunk_t* ChunkManager::getChunk(int32_t x, int32_t z) {
    if (mcp_chunk_t* c = getChunkIfLoaded(x,z)) return c;
    if (!provider_) return nullptr;
    mcp_chunk_t* c = nullptr; // provider_->loadChunk(x, z);  // implemented in phase 21 glue
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
            // provider_->saveChunk(*kv.second);  // implemented in phase 21 glue
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
