#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemStick : public Item {
public:
    ItemStick() : Item(280) {}
    std::string name() const override { return "Stick"; }
};
} // namespace
