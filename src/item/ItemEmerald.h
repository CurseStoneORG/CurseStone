#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemEmerald : public Item {
public:
    ItemEmerald() : Item(388) {}
    std::string name() const override { return "Emerald"; }
};
} // namespace
