#pragma once
#include "BlockPressurePlate.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockPressurePlateStone : public BlockPressurePlate {
public:
    uint8_t id() const override { return STONE_PRESSURE_PLATE; }
    std::string name() const override { return "Stone Pressure Plate"; }
};
} // namespace
