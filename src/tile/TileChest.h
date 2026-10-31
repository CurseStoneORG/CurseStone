/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// TileChest — chest block entity. Stores 27 item slots (small chest).
// Pairs with a paired ChestTile for double-chest pairing (handled
// by the inventory layer).
//
// The backing storage is a ChestInventory (src/inventory/ChestInventory.h),
// NOT a private std::vector<ItemStack> — ChestInventory is the single
// source of truth for slot contents. Container::setItem/getItem/removeItem
// below just delegate to it. This keeps TileChest as the one object
// stored in World's tile registry (see World::getTile/setTile) instead
// of having two parallel places (a raw vector here + an Inventory
// elsewhere) that could drift out of sync.
#pragma once
#include "Tile.h"
#include "Container.h"
#include "Nameable.h"
#include "Spawnable.h"
#include "item/ItemStack.h" // required for ItemStack return/param types below
#include "inventory/ChestInventory.h"
#include <cstdint>
#include <memory>
namespace cursestone::world {
class World;
}
namespace cursestone::inventory {
class DoubleChestInventory;
}
namespace cursestone::tile {
class TileChest : public Tile, public Container, public Nameable, public Spawnable {
  public:
    TileChest(cursestone::world::Position pos)
        : Tile("Chest", pos),
          inventory_(std::make_shared<cursestone::inventory::ChestInventory>()) {
    }
    int size() const override {
        return SIZE;
    }
    void setItem(int slot, const cursestone::item::ItemStack& it) override;
    cursestone::item::ItemStack getItem(int slot) const override;
    void removeItem(int slot) override;
    void spawnTo(void* /*player*/) override {
    }
    void writeNBT(cursestone::nbt::Tag* tag) const override;
    void readNBT(const cursestone::nbt::Tag* tag) override;

    // The backing inventory. Player::openChestAt() hands this shared_ptr
    // straight to InventoryOpenEvent / ContainerSetContentPacket — no
    // copy, so edits made through either the Container interface above
    // or directly via this Inventory land in the same 27 slots. A paired
    // chest is opened through TileChest::getOrCreateDouble() instead,
    // which wraps both halves without copying any slot.
    std::shared_ptr<cursestone::inventory::ChestInventory> inventory() const {
        return inventory_;
    }

    // How many players currently have this chest open. Player::openChestAt/
    // closeOpenChest keep this in sync (drives the lid open/close events).
    // Multiple players CAN hold this chest open at once — slot changes are
    // resynced to every co-viewer via Player::broadcastChestSlotToViewers,
    // so all of them see the same live content.
    int viewerCount() const {
        return viewer_count_;
    }
    void addViewer() {
        ++viewer_count_;
    }
    void removeViewer() {
        if (viewer_count_ > 0)
            --viewer_count_;
    }

    // ---- Vanilla double-chest pairing ----
    // The pairing is the vanilla one: each half stores the integer
    // position of its partner ("pairx"/"pairz", same-Y implied) — the
    // fields persist through the tile NBT, so a double chest saved and
    // reloaded stays a double chest.
    bool isPaired() const {
        return has_pair_;
    }
    int32_t pairX() const {
        return pair_x_;
    }
    int32_t pairZ() const {
        return pair_z_;
    }

    // Resolves the paired half through the world tile registry. Returns
    // null when this chest is single, the chunk is not loaded, or the
    // registered tile at the pair position is not a chest anymore.
    std::shared_ptr<TileChest> pairTile(cursestone::world::World* lvl) const;

    // Vanilla Chest::createPair — writes the pairing onto BOTH halves and
    // invalidates the cached double inventories.
    void setPair(const std::shared_ptr<TileChest>& other);

    // Drops the pairing state of THIS half only (pair position + cached
    // double inventory) and marks the tile dirty for the next save.
    void clearPair();

    // Vanilla InventoryLargeChest for the pair a/b — built once and SHARED
    // by both tiles so every viewer of either half works on the same 54
    // slot space. Slot order follows the vanilla orientation rule: the
    // east half of an X-axis pair (and the north half of a Z-axis pair)
    // addresses slots 0-26. Returns null when a/b are not a valid pair.
    static std::shared_ptr<cursestone::inventory::DoubleChestInventory> getOrCreateDouble(
        const std::shared_ptr<TileChest>& a, const std::shared_ptr<TileChest>& b);

  private:
    static constexpr int SIZE = 27;
    std::shared_ptr<cursestone::inventory::ChestInventory> inventory_;
    int viewer_count_ = 0;
    // Vanilla "pairx"/"pairz" — the paired half's block position (same Y).
    bool has_pair_ = false;
    int32_t pair_x_ = 0;
    int32_t pair_z_ = 0;
    // Cached double window, always shared by both halves when set.
    std::shared_ptr<cursestone::inventory::DoubleChestInventory> double_;
};
} // namespace
