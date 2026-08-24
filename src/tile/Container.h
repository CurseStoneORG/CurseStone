// Container — interface for tiles that expose an inventory (chest,
// furnace, brewing stand). Mirrors pocketmine/tile/Container.
#pragma once
#include <vector>
#include <cstdint>
namespace atomikpe::item { struct ItemStack; }
namespace atomikpe::tile {
class Container {
public:
    virtual ~Container() = default;
    virtual int size() const = 0;
    virtual int maxSize() const { return 64; }
    virtual void setItem(int slot, const atomikpe::item::ItemStack& it) = 0;
    virtual atomikpe::item::ItemStack getItem(int slot) const = 0;
    virtual void clear() { for (int i = 0; i < size(); ++i) removeItem(i); }
    virtual void removeItem(int slot) = 0;
};
} // namespace
