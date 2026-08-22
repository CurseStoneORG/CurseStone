#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockCoalOre : public BlockSolid {
public:
    uint8_t id() const override { return COAL_ORE; }
    std::string name() const override { return "Coal Ore"; }
};
} // namespace
