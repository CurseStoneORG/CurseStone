#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemCoal : public Item {
public:
    ItemCoal() : Item(263) {}
    std::string name() const override { return "Coal"; }
};
} // namespace
