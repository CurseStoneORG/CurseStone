#pragma once
#include "ItemFood.h"
#include <string>
namespace mcppeserver::item {
class ItemCookedPorkchop : public ItemFood {
public:
    ItemCookedPorkchop() : ItemFood(320, 8) {}
    std::string name() const override { return "Cooked Porkchop"; }
};
} // namespace
