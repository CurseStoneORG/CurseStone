#pragma once
#include "BlockSolid.h"
#include <string>
namespace mcppeserver::block {
class BlockEmeraldOre : public BlockSolid {
public:
    uint8_t id() const override { return 129; }
    std::string name() const override { return "Emerald Ore"; }
};
} // namespace
