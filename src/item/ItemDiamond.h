#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemDiamond : public Item {
public:
    ItemDiamond() : Item(264) {}
    std::string name() const override { return "Diamond"; }
};
} // namespace
