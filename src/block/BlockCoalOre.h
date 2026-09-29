#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockCoalOre : public BlockSolid {
public:
    uint8_t id() const override { return COAL_ORE; }
    std::string name() const override { return "Coal Ore"; }
};
} // namespace
