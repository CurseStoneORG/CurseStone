#pragma once
#include "Item.h"
#include <string>
namespace mcppeserver::item {
class ItemFeather : public Item {
public:
    ItemFeather() : Item(288) {}
    std::string name() const override { return "Feather"; }
};
} // namespace
