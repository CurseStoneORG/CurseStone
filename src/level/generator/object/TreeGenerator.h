// TreeGenerator — places a small tree (oak or birch) at chunk-local
// coordinates. The trunk is 4-6 blocks of Log (block 17), capped by
// a leaf blob (block 18). Trees that overflow their own chunk are
// not pruned (matching PocketMine behaviour).
#pragma once
#include "level/format/Chunk.h"
#include <cstdint>

namespace cursestone::level::generator::object {
enum TreeType { TREE_OAK = 0, TREE_BIRCH = 1, TREE_SPRUCE = 2 };
class TreeGenerator {
public:
    static bool place(mcp_chunk_t* c, int x, int y, int z, TreeType t);
};
} // namespace
