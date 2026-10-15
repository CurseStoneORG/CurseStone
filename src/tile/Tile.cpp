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
#include "Tile.h"
#include "nbt/tag/CompoundTag.h"
namespace cursestone::tile {
void Tile::writeNBT(mcp_tag_t* tag) const {
    if (!tag)
        return;
    mcp_compound_tag_set_string(tag, "id", id_.c_str());
    mcp_compound_tag_set_int(tag, "x", static_cast<int32_t>(pos_.x));
    mcp_compound_tag_set_int(tag, "y", static_cast<int32_t>(pos_.y));
    mcp_compound_tag_set_int(tag, "z", static_cast<int32_t>(pos_.z));
}
void Tile::readNBT(const mcp_tag_t* /*tag*/) {
    // x/y/z/id are already known by the time a subclass's readNBT runs
    // (World::ensureTilesLoaded reads them from the tag itself to decide
    // where to construct the Tile, before construction happens) — nothing
    // left to do at the base-class level.
}
} // namespace
