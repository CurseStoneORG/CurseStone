#pragma once
#include "BlockLiquid.h"
#include <string>
#include "BlockIds.h"
namespace cenisyspe::block {
class BlockWater : public BlockLiquid {
public:
    uint8_t id() const override { return WATER; }
    std::string name() const override { return "Water"; }
};
} // namespace
