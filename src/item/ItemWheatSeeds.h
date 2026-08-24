#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemWheatSeeds : public Item {
public:
    ItemWheatSeeds() : Item(295) {}
    std::string name() const override { return "Wheat Seeds"; }
};
} // namespace
