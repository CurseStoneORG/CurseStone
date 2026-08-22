#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockStoneBricks : public BlockSolid {
public:
    uint8_t id() const override { return STONE_BRICKS; }
    std::string name() const override { return "Stone Bricks"; }
};
} // namespace
