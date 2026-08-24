#pragma once
#include "BlockTransparent.h"
namespace atomikpe::block {
class BlockLiquid : public BlockTransparent {
public:
    bool isSolid() const override { return false; }
    bool canBeReplaced() const override { return true; }
    bool canWalkThrough() const override { return true; }
    float getHardness() const override { return 100.0f; }
    float getResistance() const override { return 500.0f; }
    atomikpe::math::AxisAlignedBB getBoundingBox() const override { return {0,0,0, 1, 0.9f, 1}; }
    virtual bool isWater() const { return false; }
    virtual bool isLava() const { return false; }
};
} // namespace
