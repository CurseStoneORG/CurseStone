#pragma once
#include "Item.h"
#include <string>
namespace cursestone::item {
class ItemWheat : public Item {
public:
    ItemWheat() : Item(296) {}
    std::string name() const override { return "Wheat"; }
};
} // namespace
