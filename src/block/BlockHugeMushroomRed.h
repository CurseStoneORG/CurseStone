#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockHugeMushroomRed : public BlockSolid {
public:
    uint8_t id() const override { return HUGE_MUSHROOM_RED; }
    std::string name() const override { return "Huge Red Mushroom"; }
};
} // namespace
