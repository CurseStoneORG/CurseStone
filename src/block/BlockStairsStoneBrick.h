#pragma once
#include "BlockStairs.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockStairsStoneBrick : public BlockStairs {
public:
    uint8_t id() const override { return STONE_BRICK_STAIRS; }
    std::string name() const override { return "Stone Brick Stairs"; }
};
} // namespace
