#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemEgg : public Item {
public:
    ItemEgg() : Item(344) {}
    std::string name() const override { return "Egg"; }
};
} // namespace
