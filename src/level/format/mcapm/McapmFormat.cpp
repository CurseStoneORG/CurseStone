// SPDX-License-Identifier: LGPL-3.0-or-later
// McapmFormat — Phase 8 stub implementation.
#include "McapmFormat.h"
#include "../LevelProviderManager.h"

namespace cenisyspe::level::format::mcapm {

McapmRegistrar g_mcapm_registrar_;
McapmRegistrar::McapmRegistrar() {
    LevelProviderManager::getInstance().registerProvider(
        "mcapm", [](){ return std::unique_ptr<LevelProvider>(new McapmFormat()); });
}

McapmFormat::McapmFormat() = default;
McapmFormat::~McapmFormat() { shutdown(); }

void McapmFormat::init(const std::string& path) {
    path_ = path;
    // TODO post-1.0: enumerate region files for MCAPM format.
}

void McapmFormat::shutdown() {
    // Phase 8 stub.
}

std::optional<std::shared_ptr<FullChunk>>
McapmFormat::loadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Phase 8 stub.
    return std::nullopt;
}

void McapmFormat::saveChunk(const std::shared_ptr<FullChunk>& /*chunk*/) {
    // Phase 8 stub.
}

void McapmFormat::unloadChunk(int32_t /*x*/, int32_t /*z*/) {
    // Phase 8 stub.
}

std::vector<std::pair<int32_t, int32_t>>
McapmFormat::listChunks() const {
    return {};
}

} // namespace cenisyspe::level::format::mcapm
