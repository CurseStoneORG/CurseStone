// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelProviderManager — implementation.
//
// PocketMine's LevelProviderManager is essentially a static map; we
// mirror that with a singleton + a std::unordered_map. The actual
// registration happens in each provider's .cpp file via a static
// initializer (e.g. atmkma/AtmkMca.cpp has a `static bool
// registered_atmkma = LevelProviderManager::getInstance()
//     .registerProvider("atmkma", [](){...});`).
#include "LevelProviderManager.h"

#include <algorithm>

namespace atomikpe::level::format {

LevelProviderManager& LevelProviderManager::getInstance() {
    // Meyers singleton — thread-safe in C++11+.
    static LevelProviderManager instance;
    return instance;
}

bool LevelProviderManager::registerProvider(const std::string& name,
                                              ProviderFactory factory) {
    if (!factory) return false;
    return registry_.emplace(name, std::move(factory)).second;
}

ProviderFactory
LevelProviderManager::getFactory(const std::string& name) const {
    auto it = registry_.find(name);
    if (it == registry_.end()) return nullptr;
    return it->second;
}

std::unique_ptr<LevelProvider>
LevelProviderManager::create(const std::string& name) const {
    auto factory = getFactory(name);
    if (!factory) return nullptr;
    return factory();
}

std::vector<std::string> LevelProviderManager::listFormats() const {
    std::vector<std::string> out;
    out.reserve(registry_.size());
    for (const auto& [name, _] : registry_) {
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

const char* LevelProviderManager::defaultFormatName() {
    return "atmkma";
}

} // namespace atomikpe::level::format
