#pragma once
#include "BlockWeightedPressurePlate.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockWeightedPressurePlateLight : public BlockWeightedPressurePlate {
public:
    uint8_t id() const override { return WEIGHTED_PRESSURE_PLATE_LIGHT; }
    std::string name() const override { return "Weighted Pressure Plate (Light)"; }
};
} // namespace
