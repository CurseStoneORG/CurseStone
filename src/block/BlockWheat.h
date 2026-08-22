#pragma once
#include "BlockTransparent.h"
namespace mcppeserver::block {
class BlockWheat : public BlockTransparent {
public:
    uint8_t id() const override { return 59; }
    std::string name() const override { return "Wheat Crops"; }
};
} // namespace
