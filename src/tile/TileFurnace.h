// TileFurnace — furnace block entity. 3 slots: input, fuel, output.
// Burns fuel at 1 tick per 10 ticks; smelting progress at 1 tick
// per 200 ticks. The state of the burn/cook progress is sent to
// clients via ContainerSetDataPacket.
#pragma once
#include "Tile.h"
#include "Container.h"
#include "Nameable.h"
#include "Spawnable.h"
#include "item/ItemStack.h"   // required for std::vector<ItemStack> member below
#include <vector>
namespace cursestone::tile {
class TileFurnace : public Tile, public Container, public Nameable, public Spawnable {
public:
    TileFurnace(cursestone::level::Position pos) : Tile("Furnace", pos) {
        contents_.resize(3);
    }
    int size() const override { return 3; }
    void setItem(int slot, const cursestone::item::ItemStack& it) override;
    cursestone::item::ItemStack getItem(int slot) const override;
    void removeItem(int slot) override;
    void spawnTo(void* /*player*/) override {}
    void tick(uint64_t /*t*/) override;
    int burnTime() const { return burn_time_; }
    int cookTime() const { return cook_time_; }
    int maxBurnTime() const { return 200; }
    int maxCookTime() const { return 200; }
private:
    std::vector<cursestone::item::ItemStack> contents_;
    int burn_time_ = 0;
    int cook_time_ = 0;
};
} // namespace
