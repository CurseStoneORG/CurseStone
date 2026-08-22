#pragma once
#include "Item.h"
namespace mcppeserver::item {
class ItemSugar : public Item {
public:
    ItemSugar() : Item(353) {}
    std::string name() const override { return "Sugar"; }
};
} // namespace
