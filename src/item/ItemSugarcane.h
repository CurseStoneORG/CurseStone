#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemSugarcane : public Item {
public:
    ItemSugarcane() : Item(338) {}
    std::string name() const override { return "Sugarcane"; }
};
} // namespace
