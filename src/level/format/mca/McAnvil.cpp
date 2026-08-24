// SPDX-License-Identifier: LGPL-3.0-or-later
// McAnvil.cpp — alias provider that delegates to AtmkMca.
//
// "mcanvil" and "mca" config names are kept for backward compat with
// old configs that referenced them. They all instantiate AtmkMca,
// which is now the canonical AtomikPE world format (replacing McaCpp).
#include "McAnvil.h"
#include "../LevelProviderManager.h"
#include "../atmkma/AtmkMca.h"

#include <cstdio>
#include <memory>

namespace atomikpe::level::format::mca {

McAnvilRegistrar g_mca_registrar_;
McAnvilRegistrar::McAnvilRegistrar() {
    // NOTE: AtmkMcaRegistrar (in atmkma/AtmkMca.cpp) already registers
    // "atmkma", "mcacpp", "mcanvil", and "mca" — so the explicit
    // registrations here are redundant. We keep them as documentation.
    LevelProviderManager::getInstance().registerProvider(
        "mcanvil", [](){ return std::unique_ptr<LevelProvider>(new McAnvil()); });
    LevelProviderManager::getInstance().registerProvider(
        "mca", [](){ return std::unique_ptr<LevelProvider>(new McAnvil()); });
}

McAnvil::McAnvil() {
    inner_ = std::make_unique<atmkma::AtmkMca>();
}

McAnvil::~McAnvil() { shutdown(); }

void McAnvil::init(const std::string& path) {
    if (inner_) inner_->init(path);
}

void McAnvil::shutdown() {
    if (inner_) inner_->shutdown();
}

std::string McAnvil::regionPath(int32_t cx, int32_t cz) const {
    int32_t rx = (cx >= 0) ? (cx / 32) : ((cx - 31) / 32);
    int32_t rz = (cz >= 0) ? (cz / 32) : ((cz - 31) / 32);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "regions/r.%d.%d.atmka", (int)rx, (int)rz);
    return std::string(buf);
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
