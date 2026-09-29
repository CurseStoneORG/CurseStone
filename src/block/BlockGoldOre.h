#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockGoldOre : public BlockSolid {
public:
    uint8_t id() const override { return GOLD_ORE; }
    std::string name() const override { return "Gold Ore"; }
};
} // namespace
