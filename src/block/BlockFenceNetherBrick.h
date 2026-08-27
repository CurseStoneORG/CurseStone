#pragma once
#include "BlockFence.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockFenceNetherBrick : public BlockFence {
public:
    uint8_t id() const override { return FENCE_NETHER_BRICK; }
    std::string name() const override { return "Nether Brick Fence"; }
};
} // namespace
