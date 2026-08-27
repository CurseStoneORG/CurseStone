#pragma once
#include "BlockStairs.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockStairsQuartz : public BlockStairs {
public:
    uint8_t id() const override { return QUARTZ_STAIRS; }
    std::string name() const override { return "Quartz Stairs"; }
};
} // namespace
