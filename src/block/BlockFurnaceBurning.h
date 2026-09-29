#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockFurnaceBurning : public BlockSolid {
public:
    uint8_t id() const override { return FURNACE_BURNING; }
    std::string name() const override { return "Burning Furnace"; }
};
} // namespace
