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
    TileSign(cursestone::world::Position pos) : Tile("Sign", pos) {
    }
    const std::array<std::string, 4>& lines() const {
        return lines_;
    }
    const std::string& line(int idx) const {
        static const std::string kEmpty;
        return (idx >= 0 && idx < 4) ? lines_[idx] : kEmpty;
    }
    // Bounds-checked: lines_ is a fixed 4-element array and callers index it
    // with data that ultimately comes from a client packet, so an unchecked
    // write here would be an out-of-bounds write on the heap.
    void setLine(int idx, std::string text) {
        if (idx < 0 || idx >= 4)
            return;
        lines_[idx] = std::move(text);
        dirty_ = true;
    }
    // UUID del jugador que colocó el cartel (NBT "Creator"). handleSignEdit
    // lo compara contra el UUID del jugador para autorizar la edición,
    // igual que Genisys (the sign's stored creator id is compared with the player's unique id).
    const std::string& creator() const {
        return creator_;
    }
    void setCreator(std::string c) {
        creator_ = std::move(c);
        dirty_ = true;
    }
    void spawnTo(void* /*player*/) override {
    }
    void writeNBT(mcp_tag_t* tag) const override;
    void readNBT(const mcp_tag_t* tag) override;

  private:
    std::array<std::string, 4> lines_;
    std::string creator_;
};
} // namespace
