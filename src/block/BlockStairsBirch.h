#pragma once
#include "BlockStairsWood.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockStairsBirch : public BlockStairsWood {
public:
    uint8_t id() const override { return WOOD_STAIRS; }
    std::string name() const override { return "Birch Stairs"; }
};
} // namespace
