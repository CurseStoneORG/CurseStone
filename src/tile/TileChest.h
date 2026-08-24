// TileChest — chest block entity. Stores 27 item slots (small chest).
// Pairs with a paired ChestTile for double-chest pairing (handled
// by the inventory layer).
#pragma once
#include "Tile.h"
#include "Container.h"
#include "Nameable.h"
#include "Spawnable.h"
#include "item/ItemStack.h"   // required for std::vector<ItemStack> member below
#include <vector>
namespace atomikpe::tile {
class TileChest : public Tile, public Container, public Nameable, public Spawnable {
public:
    TileChest(atomikpe::level::Position pos) : Tile("Chest", pos) {
        contents_.resize(SIZE);
    }
    int size() const override { return SIZE; }
    void setItem(int slot, const atomikpe::item::ItemStack& it) override;
    atomikpe::item::ItemStack getItem(int slot) const override;
    void removeItem(int slot) override;
    void spawnTo(void* player) override {}
    void writeNBT(class CompoundTag* tag) const override;
    void readNBT(const class CompoundTag* tag) override;
private:
    static constexpr int SIZE = 27;
    std::vector<atomikpe::item::ItemStack> contents_;
    int viewer_count_ = 0;
    std::weak_ptr<TileChest> pair_;  // paired chest (double chest)
};
} // namespace
