#pragma once
#include "BlockTransparent.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockWeightedPressurePlate : public BlockTransparent {
public:
    uint8_t id() const override { return WEIGHTED_PRESSURE_PLATE_LIGHT; }
    std::string name() const override { return "Weighted Pressure Plate"; }
};
} // namespace
