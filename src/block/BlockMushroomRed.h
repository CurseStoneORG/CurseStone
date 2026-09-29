#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockMushroomRed : public BlockFlowable {
public:
    uint8_t id() const override { return RED_MUSHROOM; }
    std::string name() const override { return "Red Mushroom"; }
};
} // namespace
