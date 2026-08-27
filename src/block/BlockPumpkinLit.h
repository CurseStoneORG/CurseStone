#pragma once
#include "BlockPumpkin.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockPumpkinLit : public BlockPumpkin {
public:
    uint8_t id() const override { return PUMPKIN_LIT; }
    std::string name() const override { return "Jack o'Lantern"; }
};
} // namespace
