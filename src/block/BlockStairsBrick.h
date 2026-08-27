#pragma once
#include "BlockStairs.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockStairsBrick : public BlockStairs {
public:
    uint8_t id() const override { return BRICK_STAIRS; }
    std::string name() const override { return "Brick Stairs"; }
};
} // namespace
