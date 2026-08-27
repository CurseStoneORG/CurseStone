#pragma once
#include "BlockStairs.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockStairsCobblestone : public BlockStairs {
public:
    uint8_t id() const override { return COBBLE_STAIRS; }
    std::string name() const override { return "Cobblestone Stairs"; }
};
} // namespace
