// ChunkLoader — abstract consumer of chunk updates.
// A Player is the canonical ChunkLoader: it asks the World to load
// chunks within view distance and the World pushes FullChunkDataPacket
// to it. Mirrors pocketmine/level/ChunkLoader interface.
#pragma once
#include "world/format/Chunk.h"
namespace cursestone::world {
class ChunkLoader {
public:
    virtual ~ChunkLoader() = default;
    virtual void onChunkLoaded(int32_t x, int32_t z, const mcp_chunk_t* c) = 0;
    virtual int32_t viewDistance() const { return 8; }
    virtual bool isAlive() const { return true; }
};
} // namespace
