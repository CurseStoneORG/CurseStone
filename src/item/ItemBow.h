#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemBow : public Item {
public:
    ItemBow() : Item(261) {}
    std::string name() const override { return "Bow"; }
};
} // namespace
