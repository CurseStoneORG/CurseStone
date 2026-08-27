#pragma once
#include "BlockThin.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockIronBars : public BlockThin {
public:
    uint8_t id() const override { return IRON_BARS_ID; }
    std::string name() const override { return "Iron Bars"; }
};
} // namespace
