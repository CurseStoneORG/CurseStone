#pragma once
#include "ItemFood.h"
namespace mcppeserver::item {
class ItemCookedChicken : public ItemFood {
public:
    ItemCookedChicken() : ItemFood(366, 6) {}
    std::string name() const override { return "Cooked Chicken"; }
};
} // namespace
