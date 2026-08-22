#pragma once
#include "ItemFood.h"
namespace mcppeserver::item {
class ItemRawChicken : public ItemFood {
public:
    ItemRawChicken() : ItemFood(366, 2) {}
    std::string name() const override { return "Raw Chicken"; }
};
} // namespace
