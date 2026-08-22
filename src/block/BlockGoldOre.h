#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockGoldOre : public BlockSolid {
public:
    uint8_t id() const override { return GOLD_ORE; }
    std::string name() const override { return "Gold Ore"; }
};
} // namespace
