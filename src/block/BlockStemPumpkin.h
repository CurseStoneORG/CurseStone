#pragma once
#include "BlockCrops.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockStemPumpkin : public BlockCrops {
public:
    uint8_t id() const override { return PUMPKIN_STEM; }
    std::string name() const override { return "Pumpkin Stem"; }
};
} // namespace
