#pragma once
#include "BlockSolid.h"
namespace mcppeserver::block {
class BlockGrass : public BlockSolid {
public:
    uint8_t id() const override { return GRASS; }
    std::string name() const override { return "Grass Block"; }
};
} // namespace
