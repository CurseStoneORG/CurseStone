#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cenisyspe::block {
class BlockWoodStairs : public BlockSolid {
public:
    uint8_t id() const override { return WOOD_STAIRS; }
    std::string name() const override { return "Wood Stairs"; }
};
} // namespace
