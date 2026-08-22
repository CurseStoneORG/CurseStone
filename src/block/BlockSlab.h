#pragma once
#include "BlockTransparent.h"
namespace mcppeserver::block {
class BlockSlab : public BlockTransparent {
public:
    uint8_t id() const override { return 44; }
    std::string name() const override { return "Slab"; }
};
} // namespace
