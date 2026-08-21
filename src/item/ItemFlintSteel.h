#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemFlintSteel : public Item {
public:
    ItemFlintSteel() : Item(259) {}
    std::string name() const override { return "Flint and Steel"; }
};
} // namespace
