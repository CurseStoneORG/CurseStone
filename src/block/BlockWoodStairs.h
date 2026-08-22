#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockWoodStairs : public BlockSolid {
public:
    uint8_t id() const override { return WOOD_STAIRS; }
    std::string name() const override { return "Wood Stairs"; }
};
} // namespace
