#pragma once
#include "BlockSolid.h"
namespace mcppeserver::block {
class BlockLapisOre : public BlockSolid {
public:
    uint8_t id() const override { return 21; }
    std::string name() const override { return "Lapis Lazuli Ore"; }
};
} // namespace
