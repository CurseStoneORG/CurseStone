#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemRedstone : public Item {
public:
    ItemRedstone() : Item(331) {}
    std::string name() const override { return "Redstone Dust"; }
};
} // namespace
