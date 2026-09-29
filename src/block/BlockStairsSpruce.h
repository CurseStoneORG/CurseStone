#pragma once
#include "BlockStairsWood.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockStairsSpruce : public BlockStairsWood {
public:
    uint8_t id() const override { return WOOD_STAIRS; }
    std::string name() const override { return "Spruce Stairs"; }
};
} // namespace
