#pragma once
#include "BlockPressurePlate.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockPressurePlateWood : public BlockPressurePlate {
public:
    uint8_t id() const override { return WOOD_PRESSURE_PLATE; }
    std::string name() const override { return "Wood Pressure Plate"; }
};
} // namespace
