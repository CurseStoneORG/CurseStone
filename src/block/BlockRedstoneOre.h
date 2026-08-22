#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockRedstoneOre : public BlockSolid {
public:
    uint8_t id() const override { return REDSTONE_ORE; }
    std::string name() const override { return "Redstone Ore"; }
};
} // namespace
