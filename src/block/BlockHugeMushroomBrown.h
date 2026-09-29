#pragma once
#include "BlockSolid.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockHugeMushroomBrown : public BlockSolid {
public:
    uint8_t id() const override { return HUGE_MUSHROOM_BROWN; }
    std::string name() const override { return "Huge Brown Mushroom"; }
};
} // namespace
