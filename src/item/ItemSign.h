#pragma once
#include "Item.h"
#include <string>
namespace atomikpe::item {
class ItemSign : public Item {
public:
    ItemSign() : Item(323) {}
    std::string name() const override { return "Sign"; }
};
} // namespace
