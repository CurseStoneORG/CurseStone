#pragma once
#include "BlockSolid.h"
#include "BlockIds.h"
namespace mcppeserver::block {
class BlockBedrock : public BlockSolid {
public:
    uint8_t id() const override { return BEDROCK; }
    float getHardness() const override { return -1; }      // unbreakable
    float getResistance() const override { return 18000000; }
    std::string name() const override { return "Bedrock"; }
};
} // namespace
