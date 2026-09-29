#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockPressurePlate : public BlockTransparent {
public:
    uint8_t id() const override { return WOOD_PRESSURE_PLATE; }
    std::string name() const override { return "Pressure Plate"; }
};
} // namespace
