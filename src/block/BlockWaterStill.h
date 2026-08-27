#pragma once
#include "BlockWater.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockWaterStill : public BlockWater {
public:
    uint8_t id() const override { return WATER_STILL; }
    std::string name() const override { return "Still Water"; }
};
} // namespace
