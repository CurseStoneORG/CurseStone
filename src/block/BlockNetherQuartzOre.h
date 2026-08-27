#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockNetherQuartzOre : public BlockSolid {
public:
    uint8_t id() const override { return QUARTZ_ORE; }
    std::string name() const override { return "Nether Quartz Ore"; }
};
} // namespace
