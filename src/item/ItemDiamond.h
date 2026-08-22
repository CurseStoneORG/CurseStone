#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemDiamond : public Item {
public:
    ItemDiamond() : Item(264) {}
    std::string name() const override { return "Diamond"; }
};
} // namespace
