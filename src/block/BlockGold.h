#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockGold : public BlockSolid {
public:
    uint8_t id() const override { return GOLD_BLOCK; }
    std::string name() const override { return "Gold Block"; }
};
} // namespace
