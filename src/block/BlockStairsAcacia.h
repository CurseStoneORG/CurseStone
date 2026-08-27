#pragma once
#include "BlockStairsWood.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockStairsAcacia : public BlockStairsWood {
public:
    uint8_t id() const override { return WOOD_STAIRS; }
    std::string name() const override { return "Acacia Stairs"; }
};
} // namespace
