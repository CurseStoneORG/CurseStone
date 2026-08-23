#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemFlintSteel : public Item {
public:
    ItemFlintSteel() : Item(259) {}
    std::string name() const override { return "Flint and Steel"; }
};
} // namespace
