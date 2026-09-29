#pragma once
#include "BlockStairs.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockNetherBrickStairs : public BlockStairs {
public:
    uint8_t id() const override { return NETHER_BRICK_STAIRS; }
    std::string name() const override { return "Nether Brick Stairs"; }
};
} // namespace
