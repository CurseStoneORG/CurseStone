#pragma once
#include "BlockLiquid.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockLava : public BlockLiquid {
public:
    uint8_t id() const override { return LAVA; }
    std::string name() const override { return "Lava"; }
};
} // namespace
