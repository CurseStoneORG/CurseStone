#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemFlint : public Item {
public:
    ItemFlint() : Item(318) {}
    std::string name() const override { return "Flint"; }
};
} // namespace
