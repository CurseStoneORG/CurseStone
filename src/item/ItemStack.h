// ItemStack — lightweight value type used by Inventory / Tile
// containers. Holds (item_id, meta, count). Conversion to/from
// the polymorphic Item* happens at the inventory boundary.
#pragma once
#include <cstdint>
namespace cursestone::item {
struct ItemStack {
    uint16_t id = 0;
    uint8_t  meta = 0;
    int      count = 0;
    bool isEmpty() const { return count <= 0 || id == 0; }
    static ItemStack EMPTY() { return {0, 0, 0}; }
};
} // namespace
