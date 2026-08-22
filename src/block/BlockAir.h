#pragma once
#include "Block.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockAir : public Block {
public:
    uint8_t id() const override { return AIR; }
    bool isSolid() const override { return false; }
    bool isTransparent() const override { return true; }
    bool canBeReplaced() const override { return true; }
    bool canWalkThrough() const override { return true; }
    float getHardness() const override { return 0; }
    std::string name() const override { return "Air"; }
    mcppeserver::math::AxisAlignedBB getBoundingBox() const override { return {0,0,0, 0,0,0}; }
};
} // namespace
