#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemStick : public Item {
public:
    ItemStick() : Item(280) {}
    std::string name() const override { return "Stick"; }
};
} // namespace
