#pragma once
#include "BlockRail.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockRailActivator : public BlockRail {
public:
    uint8_t id() const override { return RAIL_ACTIVATOR; }
    std::string name() const override { return "Activator Rail"; }
};
} // namespace
