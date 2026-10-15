/*
 _____                     _____ _
/  __ \                   /  ___| |
| |   | | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
// PortalRegistry — persistent list of the nether portals of one world.
//
// Every known portal is stored as its anchor block (any block of the
// portal interior) plus its axis metadata. The registry is what makes
// cross-dimension travel stable: the destination search consults it
// before touching blocks, so portals are reused instead of duplicated,
// and the list survives restarts (plain text file, one portal per line:
// "x y z axis").
#pragma once
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace cursestone::world {

class PortalRegistry {
  public:
    struct Portal {
        int32_t x = 0, y = 0, z = 0; // anchor portal block
        uint8_t axis = 0;            // portal block metadata
    };

    // Nearest portal to (x,y,z) within maxRadius (block units, weighted
    // so a vertical offset matters less than a horizontal one).
    // Returns nullptr when nothing is within range.
    const Portal* findNearest(double x, double y, double z, double maxRadius) const;

    // Adds a portal unless an entry already exists within a few blocks
    // of it (dedupe keeps repeated travels from growing the file).
    void add(int32_t x, int32_t y, int32_t z, uint8_t axis);

    // True when a stored portal sits on (x,y,z).
    bool contains(int32_t x, int32_t y, int32_t z) const;

    // Drops the entry sitting on (x,y,z), if any. Returns true when an
    // entry was removed (stale portals mined away by players).
    bool remove(int32_t x, int32_t y, int32_t z);

    // Reads/writes "<world dir>/portals.dat". load() tolerates missing
    // or malformed files (fresh worlds have none). save() writes the
    // whole list atomically enough for its size (tmp file + rename).
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    size_t size() const;

  private:
    mutable std::mutex mtx_;
    std::vector<Portal> portals_;
};

} // namespace cursestone::world
