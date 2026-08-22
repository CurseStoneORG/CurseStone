#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockDirt : public BlockSolid {
public:
    uint8_t id() const override { return DIRT; }
    std::string name() const override { return "Dirt"; }
};
} // namespace
