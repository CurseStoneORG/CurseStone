#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemCoal : public Item {
public:
    ItemCoal() : Item(263) {}
    std::string name() const override { return "Coal"; }
};
} // namespace
