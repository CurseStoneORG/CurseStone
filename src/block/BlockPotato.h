#pragma once
#include "BlockCrops.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockPotato : public BlockCrops {
public:
    uint8_t id() const override { return POTATO; }
    std::string name() const override { return "Potato"; }
};
} // namespace
