#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockFurnace : public BlockSolid {
public:
    uint8_t id() const override { return FURNACE; }
    std::string name() const override { return "Furnace"; }
};
} // namespace
