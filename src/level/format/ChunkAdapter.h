// SPDX-License-Identifier: LGPL-3.0-or-later
// ChunkAdapter — C++ FullChunk wrapper around the pure-C mcp_chunk_t.
//
// All storage formats (anvil / leveldb) use the same in-memory chunk
// struct (`mcp_chunk_t` from Chunk.h). This adapter exposes that struct
// through the C++ `FullChunk` interface so the Level / chunk-send
// pipeline / network serializer can treat chunks polymorphically
// without knowing which on-disk format produced them.
//
// Ownership: the adapter owns its `mcp_chunk_t*` and frees it on
// destruction. If you want to hand the buffer to C code without
// copying, use `releaseChunk()` to transfer ownership.
#pragma once

#include <memory>
#include <string>
#include <vector>
#include "../../main.h"
#include "FullChunk.h"
#include "Chunk.h"

namespace atomikpe::level::format {

class ChunkAdapter : public FullChunk {
public:
    // Construct an empty chunk at (x, z). Allocates the underlying
    // `mcp_chunk_t` via mcp_chunk_new.
    explicit ChunkAdapter(int32_t x, int32_t z);

    // Construct an adapter around an existing `mcp_chunk_t*`. The
    // adapter takes ownership and frees it on destruction.
    explicit ChunkAdapter(mcp_chunk_t* c);

    // Construct a BORROW-ONLY adapter: the adapter does NOT free the
    // chunk on destruction. Use this to wrap an mcp_chunk_t* that is
    // owned elsewhere (e.g. ChunkManager::chunks_) for a single call.
    static std::shared_ptr<ChunkAdapter> borrow(mcp_chunk_t* c);

    ~ChunkAdapter() override;

    // Non-copyable (owns raw pointer).
    ChunkAdapter(const ChunkAdapter&) = delete;
    ChunkAdapter& operator=(const ChunkAdapter&) = delete;

    // Movable (transfers chunk pointer).
    ChunkAdapter(ChunkAdapter&&) noexcept;
    ChunkAdapter& operator=(ChunkAdapter&&) noexcept;

    // Borrow-only access to the underlying C struct. The pointer
    // remains owned by the adapter.
    mcp_chunk_t*       chunk()       { return chunk_; }
    const mcp_chunk_t* chunk() const { return chunk_; }

    // Release ownership. Caller must mcp_chunk_destroy() the result.
    mcp_chunk_t* releaseChunk();

    // ----- FullChunk interface (see FullChunk.h for docs).
    int32_t      getX() const override;
    int32_t      getZ() const override;
    const char*  getProviderName() const override { return "adapter"; }

    uint8_t  getBlockId  (int x, int y, int z) const override;
    uint8_t  getBlockData(int x, int y, int z) const override;
    void     setBlock(int x, int y, int z, uint8_t id, uint8_t data) override;

    uint8_t  getSkyLight  (int x, int y, int z) const override;
    void     setSkyLight  (int x, int y, int z, uint8_t v) override;
    uint8_t  getBlockLight(int x, int y, int z) const override;
    void     setBlockLight(int x, int y, int z, uint8_t v) override;

    uint8_t  getBiome(int x, int z) const override;
    void     setBiome(int x, int z, uint8_t b) override;

    int32_t  getHeightMap(int x, int z) const override;
    void     setHeightMap(int x, int z, int32_t h) override;

    bool     isPopulated() const override;
    bool     isGenerated()  const override;
    bool     isDirty()     const override;
    void     setDirty(bool dirty) override;

    std::vector<uint8_t> networkSerialize() const override;

private:
    mcp_chunk_t* chunk_ = nullptr;
    bool          owns_chunk_ = true;  // false when created via borrow()
};

} // namespace atomikpe::level::format
