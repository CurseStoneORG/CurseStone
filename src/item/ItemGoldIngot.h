#pragma once
#include "Item.h"
#include <string>
namespace mcppeserver::item {
class ItemGoldIngot : public Item {
public:
    ItemGoldIngot() : Item(266) {}
    std::string name() const override { return "Gold Ingot"; }
};
} // namespace
