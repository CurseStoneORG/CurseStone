#pragma once
#include "Item.h"
#include <string>
namespace cursestone::item {
class ItemSugarcane : public Item {
public:
    ItemSugarcane() : Item(338) {}
    std::string name() const override { return "Sugarcane"; }
};
} // namespace
