#pragma once
#include "ItemFood.h"
namespace mcppeserver::item {
class ItemRawPorkchop : public ItemFood {
public:
    ItemRawPorkchop() : ItemFood(319, 3) {}
    std::string name() const override { return "Raw Porkchop"; }
};
} // namespace
