#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemLeather : public Item {
public:
    ItemLeather() : Item(334) {}
    std::string name() const override { return "Leather"; }
};
} // namespace
