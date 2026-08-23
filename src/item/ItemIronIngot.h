#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemIronIngot : public Item {
public:
    ItemIronIngot() : Item(265) {}
    std::string name() const override { return "Iron Ingot"; }
};
} // namespace
