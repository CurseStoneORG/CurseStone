#pragma once
#include "BlockCrops.h"
#include <string>
#include "BlockIds.h"
namespace atomikpe::block {
class BlockCarrot : public BlockCrops {
public:
    uint8_t id() const override { return CARROT; }
    std::string name() const override { return "Carrot"; }
};
} // namespace
