#pragma once
#include "ItemFood.h"
namespace mcppeserver::item {
class ItemSteak : public ItemFood {
public:
    ItemSteak() : ItemFood(364, 8) {}
    std::string name() const override { return "Steak"; }
};
} // namespace
