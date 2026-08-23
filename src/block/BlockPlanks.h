#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cenisyspe::block {
class BlockPlanks : public BlockSolid {
public:
    uint8_t id() const override { return PLANKS; }
    std::string name() const override { return "Wood Planks"; }
};
} // namespace
