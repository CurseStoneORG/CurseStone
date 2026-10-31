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
// TileFurnace — furnace block entity. 3 slots: input, fuel, output.
// Burns fuel at 1 tick per 10 ticks; smelting progress at 1 tick
// per 200 ticks. The state of the burn/cook progress is sent to
// clients via ContainerSetDataPacket.
//
// Full implementation (reference Tile/Furnace):
//   - tick() advances burn_time_/cook_time_, consumes 1 input per
//     completed smelt, consumes fuel when burn runs out and writes the
//     result to slot 2.
//   - El progreso se transmite SOLO a los jugadores con el horno
//     abierto y SOLO mientras arde (ContainerSetDataPacket property
//     0/1, escala 0..200). Un horno apagado manda a lo sumo un par
//     final en ceros y se calla: replicar (0,0) cada tick es un flood
//     de 0x2D que tumba al cliente.
//   - Persistencia NBT legacy de Nukkit: { BurnDuration, BurnTicks,
//     CookTicks, Items: [{Slot,id,Damage,Count}] }.
#pragma once
#include "Tile.h"
#include "Container.h"
#include "Nameable.h"
#include "Spawnable.h"
#include "item/ItemStack.h" // required for std::vector<ItemStack> member below
#include <vector>
namespace cursestone::inventory {
class FurnaceInventory;
class CraftingManager;
}
namespace cursestone::tile {
class TileFurnace : public Tile, public Container, public Nameable, public Spawnable {
  public:
    // `recipes` defaults to the server's CraftingManager; passing one
    // explicitly is only used by the verification harness, which has no
    // booted server to ask.
    explicit TileFurnace(cursestone::world::Position pos,
                         const cursestone::inventory::CraftingManager* recipes = nullptr);
    ~TileFurnace() override;
    int size() const override {
        return 3;
    }
    void setItem(int slot, const cursestone::item::ItemStack& it) override;
    cursestone::item::ItemStack getItem(int slot) const override;
    void removeItem(int slot) override;
    void spawnTo(void* /*player*/) override {
    }
    void tick(uint64_t /*t*/) override;
    void writeNBT(mcp_tag_t* tag) const override;
    void writeSpawnNBT(mcp_tag_t* tag) const override;
    void readNBT(const mcp_tag_t* tag) override;
    int burnTime() const {
        return burn_time_;
    }
    int cookTime() const {
        return cook_time_;
    }
    int maxBurnTime() const {
        return max_burn_time_;
    }
    int maxCookTime() const {
        return 200;
    }
    // Slot indices (Nukkit FurnaceInventory).
    static constexpr int SLOT_INPUT = 0;
    static constexpr int SLOT_FUEL = 1;
    static constexpr int SLOT_RESULT = 2;
    // Register/unregister viewers (Player::openFurnaceAt/closeOpenChest
    // them) — progress is sent only to those.
    void addViewer(void* player);
    void removeViewer(void* player);
    void sendProgressToViewers();

    // The backing inventory (same architecture as TileChest::inventory
    // -> ChestInventory): the single place the 3 slots live so the
    // inventory, NBT and UI never diverge. Returned as shared_ptr
    // propietario para que los eventos de inventario nunca cuelguen.
    std::shared_ptr<cursestone::inventory::FurnaceInventory> inventory() const {
        return inv_;
    }

  private:
    // Where the smelting recipes come from; null = ask the server.
    const cursestone::inventory::CraftingManager* recipes_;
    // Swaps the block between FURNACE and BURNING_FURNACE when the fire
    // lights or goes out, and tells the players looking at it.
    void setFlame(bool lit);

    // Ticks totales que rinde ese item como fuel (0 = no es fuel).
    static int fuelBurnTicks(uint16_t id);
    // Resultado de fundir `in` (receta de horno del CraftingManager).
    // Devuelve false si ese input no se puede fundir.
    bool smeltResult(const cursestone::item::ItemStack& in, cursestone::item::ItemStack& out) const;
    // The real backing inventory. Container methods delegate here so
    // the player inventory and NBT live in one place.
    std::shared_ptr<cursestone::inventory::FurnaceInventory> inv_;
    int burn_time_ = 0;            // remaining ticks of the current fuel
    int max_burn_time_ = 0;        // total ticks of the last lit fuel (UI scale)
    int cook_time_ = 0;            // current smelt progress (0..200)
    bool dirty_inventory_ = false; // the tick must resend content
    bool lit_ = false;             // the block is already BURNING_FURNACE
    // Last furnace-property values pushed to the open window, so the
    // tick can stay silent while nothing moved. -1 = never sent yet.
    int last_sent_cook_ = -1;
    int last_sent_fuel_ = -1;
    bool progress_active_ = false; // a property pair went out at least once
    std::vector<void*> viewers_;   // raw Player*, NEVER dereference (compare only)
};
} // namespace
