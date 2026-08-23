#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemArrow : public Item {
public:
    ItemArrow() : Item(262) {}
    std::string name() const override { return "Arrow"; }
};
} // namespace
