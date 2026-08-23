// SPDX-License-Identifier: LGPL-3.0-or-later
// McAnvil — Phase 8 stub implementation. Defers to mcacpp for real I/O.
#include "McAnvil.h"
#include "../LevelProviderManager.h"

#include <cstdio>

namespace cenisyspe::level::format::mca {

McAnvilRegistrar g_mca_registrar_;
McAnvilRegistrar::McAnvilRegistrar() {
    LevelProviderManager::getInstance().registerProvider(
        "mca", [](){ return std::unique_ptr<LevelProvider>(new McAnvil()); });
}

McAnvil::McAnvil() = default;
McAnvil::~McAnvil() { shutdown(); }

void McAnvil::init(const std::string& path) {
    path_ = path;
    // TODO Phase 12: enumerate region files.
}

void McAnvil::shutdown() {
    // Phase 8 stub.
}

std::string McAnvil::regionPath(int32_t cx, int32_t cz) const {
    int32_t rx = (cx >= 0) ? (cx / 32) : ((cx - 31) / 32);
    int32_t rz = (cz >= 0) ? (cz / 32) : ((cz - 31) / 32);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s/region/r.%d.%d.mca",
                   path_.c_str(), (int)rx, (int)rz);
    return std::string(buf);
}

std::optional<std::shared_ptr<FullChunk>>
McAnvil::loadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Phase 8 stub: defer to mcacpp.
    return std::nullopt;
}

void McAnvil::saveChunk(const std::shared_ptr<FullChunk>& /*chunk*/) {
    // Phase 8 stub.
}

void McAnvil::unloadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Phase 8 stub.
}

std::vector<std::pair<int32_t, int32_t>>
McAnvil::listChunks() const {
    return {};
}

} // namespace cenisyspe::level::format::mca
