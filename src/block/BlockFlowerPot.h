#pragma once
#include "BlockFlowable.h"
#include <string>
#include "BlockIds.h"
namespace cursestone::block {
class BlockFlowerPot : public BlockFlowable {
public:
    uint8_t id() const override { return FLOWER_POT; }
    std::string name() const override { return "Flower Pot"; }
};
} // namespace
