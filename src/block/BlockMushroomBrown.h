#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockMushroomBrown : public BlockFlowable {
public:
    uint8_t id() const override { return BROWN_MUSHROOM; }
    std::string name() const override { return "Brown Mushroom"; }
};
} // namespace
