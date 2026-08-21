// TileChest — chest block entity. Stores 27 item slots (small chest).
// Pairs with a paired ChestTile for double-chest pairing (handled
// by the inventory layer).
#pragma once
#include "Tile.h"
#include "Container.h"
#include "Nameable.h"
#include "Spawnable.h"
#include <vector>
namespace mcppeserver::item { struct ItemStack; }
namespace mcppeserver::tile {
class TileChest : public Tile, public Container, public Nameable, public Spawnable {
public:
    TileChest(mcppeserver::level::Position pos) : Tile("Chest", pos) {
        contents_.resize(SIZE);
    }
    int size() const override { return SIZE; }
    void setItem(int slot, const mcppeserver::item::ItemStack& it) override;
    mcppeserver::item::ItemStack getItem(int slot) const override;
    void removeItem(int slot) override;
    void spawnTo(void* player) override {}
    void writeNBT(class CompoundTag* tag) const override;
    void readNBT(const class CompoundTag* tag) override;
private:
    static constexpr int SIZE = 27;
    std::vector<mcppeserver::item::ItemStack> contents_;
    int viewer_count_ = 0;
    std::weak_ptr<TileChest> pair_;  // paired chest (double chest)
};
} // namespace
