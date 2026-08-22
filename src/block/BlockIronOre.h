#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockIronOre : public BlockSolid {
public:
    uint8_t id() const override { return IRON_ORE; }
    std::string name() const override { return "Iron Ore"; }
};
} // namespace
