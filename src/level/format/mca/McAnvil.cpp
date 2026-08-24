// SPDX-License-Identifier: LGPL-3.0-or-later
// McAnvil — alias for McAcpp (AtomikPE's "Minecraft Anvil" format).
//
// In MCPE 0.15.x, the world format used by AtomikPE is the PMMP "McAcpp"
// format (little-endian NBT, 128-block-tall chunks, zlib-compressed
// payloads). The user-facing name is "mcanvil" — this provider delegates
// all calls to McAcpp so users can configure `format: mcanvil` in
// server.properties and it Just Works.
#include "McAnvil.h"
#include "../LevelProviderManager.h"
#include "../mcacpp/McaCpp.h"
#include "../ChunkAdapter.h"
#include "../ChunkNBTCodec.h"

#include <cstdio>
#include <memory>

namespace atomikpe::level::format::mca {

McAnvilRegistrar g_mca_registrar_;
McAnvilRegistrar::McAnvilRegistrar() {
    LevelProviderManager::getInstance().registerProvider(
        "mcanvil", [](){ return std::unique_ptr<LevelProvider>(new McAnvil()); });
    // Also register "mca" as an alias for backward compat with old configs.
    LevelProviderManager::getInstance().registerProvider(
        "mca", [](){ return std::unique_ptr<LevelProvider>(new McAnvil()); });
}

McAnvil::McAnvil() {
    // Delegate to an internal McaCpp instance.
    inner_ = std::make_unique<mcacpp::McaCpp>();
}

McAnvil::~McAnvil() { shutdown(); }

void McAnvil::init(const std::string& path) {
    if (inner_) inner_->init(path);
}

void McAnvil::shutdown() {
    if (inner_) inner_->shutdown();
}

std::string McAnvil::regionPath(int32_t cx, int32_t cz) const {
    // Use the inner McaCpp's path layout (chunks/<rx>_<rz>.mcacpp).
    // We don't use Java Anvil's r.<rx>.<rz>.mca layout because the
    // payload encoding is little-endian MCPE NBT (not Java BE NBT).
    if (inner_) {
        // McaCpp computes the region path internally; expose via init.
        // For now, return the standard mcacpp path layout.
        int32_t rx = (cx >= 0) ? (cx / 32) : ((cx - 31) / 32);
        int32_t rz = (cz >= 0) ? (cz / 32) : ((cz - 31) / 32);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "chunks/%d_%d.mcacpp", (int)rx, (int)rz);
        return std::string(buf);
    }
    return "";
}

std::optional<std::shared_ptr<FullChunk>>
McAnvil::loadChunk(int32_t x, int32_t z) {
    if (inner_) return inner_->loadChunk(x, z);
    return std::nullopt;
}

void McAnvil::saveChunk(const std::shared_ptr<FullChunk>& chunk) {
    if (inner_) inner_->saveChunk(chunk);
}

void McAnvil::unloadChunk(int32_t x, int32_t z) {
    if (inner_) inner_->unloadChunk(x, z);
}

std::vector<std::pair<int32_t, int32_t>>
McAnvil::listChunks() const {
    if (inner_) return inner_->listChunks();
    return {};
}

int64_t McAnvil::getSeed() const {
    return inner_ ? inner_->getSeed() : 0;
}
int64_t McAnvil::getCurrentTime() const {
    return inner_ ? inner_->getCurrentTime() : 0;
}
void McAnvil::setCurrentTime(int64_t t) {
    if (inner_) inner_->setCurrentTime(t);
}
math::Vector3 McAnvil::getSpawn() const {
    return inner_ ? inner_->getSpawn() : math::Vector3{0.0, 64.0, 0.0};
}
void McAnvil::setSpawn(const math::Vector3& v) {
    if (inner_) inner_->setSpawn(v);
}
void McAnvil::setGenerator(generator::Generator* g) {
    if (inner_) inner_->setGenerator(g);
}
generator::Generator* McAnvil::getGenerator() const {
    return inner_ ? inner_->getGenerator() : nullptr;
}

} // namespace atomikpe::level::format::mca
