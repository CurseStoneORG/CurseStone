// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelDB — Phase 8 stub implementation.
//
// Compiles cleanly without leveldb linked. Every chunk operation
// returns "not on disk" so Level::load() can fall back to the
// mcacpp provider when no leveldb is found.
#include "LevelDB.h"
#include "../LevelProviderManager.h"

#include <cstdint>
#include <cstring>

namespace cenisyspe::level::format::leveldb {

LevelDBRegistrar g_leveldb_registrar_;
LevelDBRegistrar::LevelDBRegistrar() {
    LevelProviderManager::getInstance().registerProvider(
        "leveldb", [](){ return std::unique_ptr<LevelProvider>(new LevelDB()); });
}

LevelDB::LevelDB() = default;
LevelDB::~LevelDB() { shutdown(); }

void LevelDB::init(const std::string& path) {
    path_ = path;
    // TODO Phase 12: open LevelDB at path_ + "/db".
}

void LevelDB::shutdown() {
    // TODO Phase 12: close LevelDB.
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
#if CENISYSPE_LEVELDB
    return true;
#else
    return false;
#endif
}

std::optional<std::shared_ptr<FullChunk>>
LevelDB::loadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Phase 8 stub: pretend the chunk isn't on disk so Level falls
    // back to chunk generation.
    return std::nullopt;
}

void LevelDB::saveChunk(const std::shared_ptr<FullChunk>& /*chunk*/) {
    // Phase 8 stub: no-op.
}

void LevelDB::unloadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Phase 8 stub: no-op.
}

std::vector<std::pair<int32_t, int32_t>>
LevelDB::listChunks() const {
    return {};
}

} // namespace cenisyspe::level::format::leveldb
