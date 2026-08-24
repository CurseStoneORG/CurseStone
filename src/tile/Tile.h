// Tile — block entity base. Tiles live in chunks as CompoundTag
// entries on disk and are synced to clients via BlockEntityDataPacket.
// Each Tile subclass (TileChest, TileFurnace, TileSign) holds its
// runtime state and writes back to NBT on save.
#pragma once
#include <cstdint>
#include <memory>
#include <string>           // required for std::string id_ and Tile(std::string, ...)
#include "level/Position.h"
namespace atomikpe::tile {
class Tile {
public:
    Tile(std::string id, atomikpe::level::Position pos)
        : id_(std::move(id)), pos_(pos) {}
    virtual ~Tile() = default;
    const std::string& getId() const { return id_; }
    const atomikpe::level::Position& position() const { return pos_; }
    void markDirty() { dirty_ = true; }
    bool isDirty() const { return dirty_; }
    virtual void tick(uint64_t /*current_tick*/) {}
    // Serialise tile state to a CompoundTag (callers free the NBT).
    virtual void writeNBT(class CompoundTag* tag) const;
    // Read tile state from a CompoundTag.
    virtual void readNBT(const class CompoundTag* tag);
protected:
    std::string id_;                  // "Chest", "Furnace", "Sign", ...
    atomikpe::level::Position pos_;
    bool dirty_ = true;
};
} // namespace
