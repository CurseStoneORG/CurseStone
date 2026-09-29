#pragma once
#include "Item.h"
#include <string>
namespace cursestone::item {
class ItemShears : public Item {
public:
    ItemShears() : Item(359) {}
    std::string name() const override { return "Shears"; }
};
} // namespace
