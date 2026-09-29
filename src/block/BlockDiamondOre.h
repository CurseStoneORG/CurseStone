#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockDiamondOre : public BlockSolid {
public:
    uint8_t id() const override { return DIAMOND_ORE; }
    std::string name() const override { return "Diamond Ore"; }
};
} // namespace
