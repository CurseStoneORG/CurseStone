#pragma once
#include "Item.h"
namespace cursestone::item {
// ItemBlock — an item that places a block when right-clicked.
class ItemBlock : public Item {
public:
    ItemBlock(uint16_t id) : Item(id) {}
    bool isTool() const override { return false; }
    int maxStackSize() const override { return 64; }
};
} // namespace
