#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemBow : public Item {
public:
    ItemBow() : Item(261) {}
    std::string name() const override { return "Bow"; }
};
} // namespace
