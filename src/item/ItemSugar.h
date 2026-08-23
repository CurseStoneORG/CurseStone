#pragma once
#include "Item.h"
#include <string>
namespace cenisyspe::item {
class ItemSugar : public Item {
public:
    ItemSugar() : Item(353) {}
    std::string name() const override { return "Sugar"; }
};
} // namespace
