#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemGunpowder : public Item {
public:
    ItemGunpowder() : Item(289) {}
    std::string name() const override { return "Gunpowder"; }
};
} // namespace
