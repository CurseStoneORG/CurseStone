#pragma once
#include "Block.h"
namespace mcppeserver::block {
class BlockTransparent : public Block {
public:
    bool isTransparent() const override { return true; }
};
} // namespace
