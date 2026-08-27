#pragma once
#include "BlockStairs.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockStairsWood : public BlockStairs {
public:
    uint8_t id() const override { return WOOD_STAIRS; }
    std::string name() const override { return "Wood Stairs"; }
};
} // namespace
