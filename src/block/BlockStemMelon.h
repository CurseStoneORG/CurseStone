#pragma once
#include "BlockCrops.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockStemMelon : public BlockCrops {
public:
    uint8_t id() const override { return MELON_STEM; }
    std::string name() const override { return "Melon Stem"; }
};
} // namespace
