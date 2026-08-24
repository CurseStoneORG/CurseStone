#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemArrow : public Item {
public:
    ItemArrow() : Item(262) {}
    std::string name() const override { return "Arrow"; }
};
} // namespace
