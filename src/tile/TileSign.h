// TileSign — sign block entity. Holds 4 lines of text. Each line
// is sent to clients via BlockEntityDataPacket.
#pragma once
#include "Tile.h"
#include "Spawnable.h"
#include <string>
#include <array>
namespace mcppeserver::tile {
class TileSign : public Tile, public Spawnable {
public:
    TileSign(mcppeserver::level::Position pos) : Tile("Sign", pos) {}
    const std::array<std::string, 4>& lines() const { return lines_; }
    void setLine(int idx, std::string text) { lines_[idx] = std::move(text); dirty_ = true; }
    void spawnTo(void* /*player*/) override {}
    void writeNBT(class CompoundTag* tag) const override;
    void readNBT(const class CompoundTag* tag) override;
private:
    std::array<std::string, 4> lines_;
};
} // namespace
