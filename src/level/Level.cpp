#include "Level.h"
#include "level/format/Chunk.h"
#include "level/format/LevelProvider.h"
#include "level/generator/Generator.h"
namespace mcppeserver::level {
Level::Level(LevelConfig cfg) : cfg_(std::move(cfg)) {
    spawn_ = math::Vector3(cfg_.spawn_x, cfg_.spawn_y, cfg_.spawn_z);
}
Level::~Level() {}
uint8_t Level::getBlockId(int32_t x, int32_t y, int32_t z) {
    int32_t cx = x >> 4, cz = z >> 4;
    mcp_chunk_t* c = chunks_.getChunkIfLoaded(cx, cz);
    if (!c) return 0;
    return mcp_chunk_get_block_id(c, x & 15, y, z & 15);
}
void Level::setBlock(int32_t x, int32_t y, int32_t z, uint8_t id, uint8_t data) {
    int32_t cx = x >> 4, cz = z >> 4;
    if (mcp_chunk_t* c = chunks_.getChunk(cx, cz)) {
        mcp_chunk_set_block_full(c, x & 15, y, z & 15, id, data);
        c->dirty = true;
    }
}
int Level::save() { return chunks_.flushDirty(); }
void Level::tick(uint64_t) {
    advanceTime(1);
}
void Level::registerLoader(uint64_t id, class ChunkLoader* loader) {
    loaders_[id] = loader;
}
void Level::unregisterLoader(uint64_t id) {
    loaders_.erase(id);
}
} // namespace
