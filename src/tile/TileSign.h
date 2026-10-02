// TileSign — sign block entity. Holds 4 lines of text. Each line
// is sent to clients via BlockEntityDataPacket.
#pragma once
#include "Tile.h"
#include "Spawnable.h"
#include <string>
#include <array>
namespace cursestone::tile {
class TileSign : public Tile, public Spawnable {
public:
    TileSign(cursestone::level::Position pos) : Tile("Sign", pos) {}
    const std::array<std::string, 4>& lines() const { return lines_; }
    void setLine(int idx, std::string text) { lines_[idx] = std::move(text); dirty_ = true; }
    // UUID del jugador que colocó el cartel (NBT "Creator"). handleSignEdit
    // lo compara contra el UUID del jugador para autorizar la edición,
    // igual que Genisys (`$t->namedtag["Creator"] !== $this->getRawUniqueId()`).
    const std::string& creator() const { return creator_; }
    void setCreator(std::string c) { creator_ = std::move(c); dirty_ = true; }
    void spawnTo(void* /*player*/) override {}
    void writeNBT(mcp_tag_t* tag) const override;
    void readNBT(const mcp_tag_t* tag) override;
private:
    std::array<std::string, 4> lines_;
    std::string creator_;
};
} // namespace
