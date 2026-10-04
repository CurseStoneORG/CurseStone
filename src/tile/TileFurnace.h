// TileFurnace — furnace block entity. 3 slots: input, fuel, output.
// Burns fuel at 1 tick per 10 ticks; smelting progress at 1 tick
// per 200 ticks. The state of the burn/cook progress is sent to
// clients via ContainerSetDataPacket.
//
// Implementación completa (PMMP Tile/Furnace.php):
//   - tick() avanza burn_time_/cook_time_, consume 1 input por
//     fundición completa, consume fuel cuando se acaba el burn y
//     escribe el resultado en el slot 2.
//   - El progreso se transmite SOLO a los jugadores con el horno
//     abierto (ContainerSetDataPacket property 0/1, escala 0..200).
//   - Persistencia NBT legacy de Nukkit: { BurnDuration, BurnTicks,
//     CookTicks, Items: [{Slot,id,Damage,Count}] }.
#pragma once
#include "Tile.h"
#include "Container.h"
#include "Nameable.h"
#include "Spawnable.h"
#include "item/ItemStack.h"   // required for std::vector<ItemStack> member below
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
    int size() const override { return 3; }
    void setItem(int slot, const cursestone::item::ItemStack& it) override;
    cursestone::item::ItemStack getItem(int slot) const override;
    void removeItem(int slot) override;
    void spawnTo(void* /*player*/) override {}
    void tick(uint64_t /*t*/) override;
    void writeNBT(mcp_tag_t* tag) const override;
    void readNBT(const mcp_tag_t* tag) override;
    int burnTime() const { return burn_time_; }
    int cookTime() const { return cook_time_; }
    int maxBurnTime() const { return max_burn_time_; }
    int maxCookTime() const { return 200; }
    // Índices de slot (Nukkit FurnaceInventory).
    static constexpr int SLOT_INPUT  = 0;
    static constexpr int SLOT_FUEL   = 1;
    static constexpr int SLOT_RESULT = 2;
    // Register/unregister viewers (Player::openFurnaceAt/closeOpenChest
    // los llaman) — el progreso se manda solo a éstos.
    void addViewer(void* player);
    void removeViewer(void* player);
    void sendProgressToViewers();

    // El backing inventory (misma arquitectura que TileChest::inventory
    // → ChestInventory): único lugar donde viven los 3 slots para que
    // inventario, NBT y UI no diverjan. Player::openFurnaceAt lo usa
    // para InventoryOpenEvent.
    const std::unique_ptr<cursestone::inventory::FurnaceInventory>& inventory() const { return inv_; }
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
    bool smeltResult(const cursestone::item::ItemStack& in,
                     cursestone::item::ItemStack& out) const;
    // El backing inventory real. Los métodos Container delegan acá
    // para que player-inventory y NBT vivan en un solo lugar.
    std::unique_ptr<cursestone::inventory::FurnaceInventory> inv_;
    int burn_time_ = 0;       // ticks restantes del fuel actual
    int max_burn_time_ = 0;   // total ticks del último fuel encendido (escala UI)
    int cook_time_ = 0;       // progreso de la fundición actual (0..200)
    bool dirty_inventory_ = false;  // el tick debe reenviar contenido
    bool lit_ = false;              // el bloque ya está en BURNING_FURNACE
    std::vector<void*> viewers_;    // Player* crudos, NO desreferenciar (solo comparar)
};
} // namespace
