#pragma once
#include "BlockLiquid.h"
namespace mcppeserver::block {
class BlockWater : public BlockLiquid {
public:
    uint8_t id() const override { return WATER; }
    std::string name() const override { return "Water"; }
};
} // namespace
