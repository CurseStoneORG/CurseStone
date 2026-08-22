#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemShears : public Item {
public:
    ItemShears() : Item(359) {}
    std::string name() const override { return "Shears"; }
};
} // namespace
