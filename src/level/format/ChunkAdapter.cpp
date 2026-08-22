// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkAdapter — implementation.
//
// Thin pass-through to the C functions in Chunk.c. Keeps the C++
// surface small (the heavy lifting lives in C).
#include "ChunkAdapter.h"

#include <utility>

namespace mcppeserver::level::format {

ChunkAdapter::ChunkAdapter(int32_t x, int32_t z) {
    chunk_ = mcp_chunk_new(x, z);
}

ChunkAdapter::ChunkAdapter(mcp_chunk_t* c) : chunk_(c) {}

ChunkAdapter::~ChunkAdapter() {
    if (chunk_) mcp_chunk_destroy(chunk_);
}

ChunkAdapter::ChunkAdapter(ChunkAdapter&& other) noexcept
    : chunk_(other.chunk_) {
    other.chunk_ = nullptr;
}

ChunkAdapter& ChunkAdapter::operator=(ChunkAdapter&& other) noexcept {
    if (this != &other) {
        if (chunk_) mcp_chunk_destroy(chunk_);
        chunk_       = other.chunk_;
        other.chunk_ = nullptr;
    }
    return *this;
}

mcp_chunk_t* ChunkAdapter::releaseChunk() {
    mcp_chunk_t* c = chunk_;
    chunk_ = nullptr;
    return c;
}

// ----- FullChunk interface

int32_t ChunkAdapter::getX() const { return chunk_ ? chunk_->x : 0; }
int32_t ChunkAdapter::getZ() const { return chunk_ ? chunk_->z : 0; }

uint8_t ChunkAdapter::getBlockId(int x, int y, int z) const {
    return mcp_chunk_get_block_id(chunk_, x, y, z);
}

uint8_t ChunkAdapter::getBlockData(int x, int y, int z) const {
    return mcp_chunk_get_block_data(chunk_, x, y, z);
}

void ChunkAdapter::setBlock(int x, int y, int z, uint8_t id, uint8_t data) {
    mcp_chunk_set_block_full(chunk_, x, y, z, id, data);
}

uint8_t ChunkAdapter::getSkyLight(int x, int y, int z) const {
    return mcp_chunk_get_sky_light(chunk_, x, y, z);
}

void ChunkAdapter::setSkyLight(int x, int y, int z, uint8_t v) {
    mcp_chunk_set_sky_light(chunk_, x, y, z, v);
}

uint8_t ChunkAdapter::getBlockLight(int x, int y, int z) const {
    return mcp_chunk_get_block_light(chunk_, x, y, z);
}

void ChunkAdapter::setBlockLight(int x, int y, int z, uint8_t v) {
    mcp_chunk_set_block_light(chunk_, x, y, z, v);
}

uint8_t ChunkAdapter::getBiome(int x, int z) const {
    return mcp_chunk_get_biome(chunk_, x, z);
}

void ChunkAdapter::setBiome(int x, int z, uint8_t b) {
    mcp_chunk_set_biome(chunk_, x, z, b);
}

int32_t ChunkAdapter::getHeightMap(int x, int z) const {
    return mcp_chunk_get_height_map(chunk_, x, z);
}

void ChunkAdapter::setHeightMap(int x, int z, int32_t h) {
    mcp_chunk_set_height_map(chunk_, x, z, h);
}

bool ChunkAdapter::isPopulated() const {
    return chunk_ ? chunk_->terrain_populated : false;
}

bool ChunkAdapter::isGenerated() const {
    return chunk_ ? (chunk_->terrain_populated && chunk_->light_populated)
                  : false;
}

bool ChunkAdapter::isDirty() const {
    return chunk_ ? chunk_->dirty : false;
}

void ChunkAdapter::setDirty(bool dirty) {
    if (chunk_) chunk_->dirty = dirty;
}

std::vector<uint8_t> ChunkAdapter::networkSerialize() const {
    if (!chunk_) return {};
    size_t len = 0;
    uint8_t* buf = mcp_chunk_network_serialize(chunk_, &len);
    if (!buf) return {};
    std::vector<uint8_t> out(buf, buf + len);
    free(buf);
    return out;
}

} // namespace mcppeserver::level::format
