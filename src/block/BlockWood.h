#pragma once
#include "BlockSolid.h"
#include <string>
namespace mcppeserver::block {
class BlockWood : public BlockSolid {
public:
    uint8_t id() const override { return 5; }
    std::string name() const override { return "Wood Planks"; }
};
} // namespace
