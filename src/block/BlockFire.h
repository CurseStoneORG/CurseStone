#pragma once
#include "BlockTransparent.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockFire : public BlockTransparent {
public:
    uint8_t id() const override { return FIRE; }
    std::string name() const override { return "Fire"; }
};
} // namespace
