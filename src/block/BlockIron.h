#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockIron : public BlockSolid {
public:
    uint8_t id() const override { return IRON_BLOCK; }
    std::string name() const override { return "Iron Block"; }
};
} // namespace
