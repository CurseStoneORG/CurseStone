#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemSugarcane : public Item {
public:
    ItemSugarcane() : Item(338) {}
    std::string name() const override { return "Sugarcane"; }
};
} // namespace
