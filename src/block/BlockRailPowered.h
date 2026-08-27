#pragma once
#include "BlockRail.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockRailPowered : public BlockRail {
public:
    uint8_t id() const override { return RAIL_POWERED; }
    std::string name() const override { return "Powered Rail"; }
};
} // namespace
