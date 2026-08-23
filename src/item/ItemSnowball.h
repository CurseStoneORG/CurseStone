#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemSnowball : public Item {
public:
    ItemSnowball() : Item(332) {}
    std::string name() const override { return "Snowball"; }
};
} // namespace
