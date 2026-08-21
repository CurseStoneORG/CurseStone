#pragma once
#include "BlockSolid.h"
namespace mcppeserver::block {
class BlockCobblestone : public BlockSolid {
public:
    uint8_t id() const override { return COBBLESTONE; }
    std::string name() const override { return "Cobblestone"; }
};
} // namespace
