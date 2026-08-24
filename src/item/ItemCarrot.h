#pragma once
#include "ItemFood.h"
#include <string>
namespace atomikpe::item {
class ItemCarrot : public ItemFood {
public:
    ItemCarrot() : ItemFood(391, 3) {}
    std::string name() const override { return "Carrot"; }
};
} // namespace
