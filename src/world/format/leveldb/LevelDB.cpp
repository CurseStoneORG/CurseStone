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
// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelDB — stub implementation.
//
// Compiles cleanly without leveldb linked. Every chunk operation
// returns "not on disk" so World::load() can fall back to the
// anvil provider when no leveldb is found.
#include "LevelDB.h"
#include "../WorldProviderManager.h"

#include <cstdint>
#include <cstring>

namespace cursestone::world::format::leveldb {

LevelDBRegistrar g_leveldb_registrar_;
LevelDBRegistrar::LevelDBRegistrar() {
    WorldProviderManager::getInstance().registerProvider(
        "leveldb", [](){ return std::unique_ptr<WorldProvider>(new LevelDB()); });
}

LevelDB::LevelDB() = default;
LevelDB::~LevelDB() { shutdown(); }

void LevelDB::init(const std::string& path) {
    path_ = path;
    // TODO: open LevelDB at path_ + "/db".
}

void LevelDB::shutdown() {
    // TODO: close LevelDB.
}

std::vector<uint8_t> LevelDB::chunkKey(int32_t cx, int32_t cz, uint8_t type) {
    // MCPE 0.15.x chunk key layout (little-endian, fixed 9 bytes):
    //   [0..4)  int32_t chunkX (LE)
    //   [4..8)  int32_t chunkZ (LE)
    //   [8]     record type byte
    std::vector<uint8_t> out(9);
    uint32_t ux = (uint32_t)cx;
    uint32_t uz = (uint32_t)cz;
    out[0] = (uint8_t)(ux);
    out[1] = (uint8_t)(ux >> 8);
    out[2] = (uint8_t)(ux >> 16);
    out[3] = (uint8_t)(ux >> 24);
    out[4] = (uint8_t)(uz);
    out[5] = (uint8_t)(uz >> 8);
    out[6] = (uint8_t)(uz >> 16);
    out[7] = (uint8_t)(uz >> 24);
    out[8] = type;
    return out;
}

bool LevelDB::hasLevelDB() {
#if CURSESTONE_LEVELDB
    return true;
#else
    return false;
#endif
}

std::optional<std::shared_ptr<FullChunk>>
LevelDB::loadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Stub: pretend the chunk isn't on disk so World falls
    // back to chunk generation.
    return std::nullopt;
}

void LevelDB::saveChunk(const std::shared_ptr<FullChunk>& /*chunk*/) {
    // Stub: no-op.
}

void LevelDB::unloadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Stub: no-op.
}

std::vector<std::pair<int32_t, int32_t>>
LevelDB::listChunks() const {
    return {};
}

} // namespace cursestone::world::format::leveldb
