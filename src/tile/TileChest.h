// TileChest — chest block entity. Stores 27 item slots (small chest).
// Pairs with a paired ChestTile for double-chest pairing (handled
// by the inventory layer).
//
// The backing storage is a ChestInventory (src/inventory/ChestInventory.h),
// NOT a private std::vector<ItemStack> — ChestInventory is the single
// source of truth for slot contents. Container::setItem/getItem/removeItem
// below just delegate to it. This keeps TileChest as the one object
// stored in Level's tile registry (see Level::getTile/setTile) instead
// of having two parallel places (a raw vector here + an Inventory
// elsewhere) that could drift out of sync.
#pragma once
#include "Tile.h"
#include "Container.h"
#include "Nameable.h"
#include "Spawnable.h"
#include "item/ItemStack.h"   // required for ItemStack return/param types below
#include "inventory/ChestInventory.h"
#include <memory>
namespace cursestone::tile {
class TileChest : public Tile, public Container, public Nameable, public Spawnable {
public:
    TileChest(cursestone::level::Position pos)
        : Tile("Chest", pos),
          inventory_(std::make_shared<cursestone::inventory::ChestInventory>()) {}
    int size() const override { return SIZE; }
    void setItem(int slot, const cursestone::item::ItemStack& it) override;
    cursestone::item::ItemStack getItem(int slot) const override;
    void removeItem(int slot) override;
    void spawnTo(void* /*player*/) override {}
    void writeNBT(mcp_tag_t* tag) const override;
    void readNBT(const mcp_tag_t* tag) override;

    // The backing inventory. Player::openChestAt() hands this shared_ptr
    // straight to InventoryOpenEvent / ContainerSetContentPacket — no
    // copy, so edits made through either the Container interface above
    // or directly via this Inventory land in the same 27 slots.
    std::shared_ptr<cursestone::inventory::ChestInventory> inventory() const { return inventory_; }

    // How many players currently have this chest open. Player::openChestAt/
    // closeOpenChest keep this in sync. Not used to gate concurrent access
    // yet (this project's chest flow is single-viewer per player, not
    // single-viewer per chest — two players CAN open the same chest at
    // once, they just don't see each other's live changes; see
    // PROMPT_CONTINUACION.md item 2 for the deliberately-small scope).
    int viewerCount() const { return viewer_count_; }
    void addViewer() { ++viewer_count_; }
    void removeViewer() { if (viewer_count_ > 0) --viewer_count_; }

private:
    static constexpr int SIZE = 27;
    std::shared_ptr<cursestone::inventory::ChestInventory> inventory_;
    int viewer_count_ = 0;
    std::weak_ptr<TileChest> pair_;  // paired chest (double chest)
};
} // namespace
