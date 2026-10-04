// SPDX-License-Identifier: LGPL-3.0-or-later
// WorldProviderManager — implementation.
//
// PocketMine's WorldProviderManager is essentially a static map; we
// mirror that with a singleton + a std::unordered_map. The actual
// registration happens in each provider's .cpp file via a static
// initializer (e.g. anvil/Anvil.cpp has a `static bool
// registered_anvil = WorldProviderManager::getInstance()
//     .registerProvider("anvil", [](){...});`).
#include "WorldProviderManager.h"

#include <algorithm>

namespace cursestone::world::format {

WorldProviderManager& WorldProviderManager::getInstance() {
    // Meyers singleton — thread-safe in C++11+.
    static WorldProviderManager instance;
    return instance;
}

bool WorldProviderManager::registerProvider(const std::string& name,
                                              ProviderFactory factory) {
    if (!factory) return false;
    return registry_.emplace(name, std::move(factory)).second;
}

ProviderFactory
WorldProviderManager::getFactory(const std::string& name) const {
    auto it = registry_.find(name);
    if (it == registry_.end()) return nullptr;
    return it->second;
}

std::unique_ptr<WorldProvider>
WorldProviderManager::create(const std::string& name) const {
    auto factory = getFactory(name);
    if (!factory) return nullptr;
    return factory();
}

std::vector<std::string> WorldProviderManager::listFormats() const {
    std::vector<std::string> out;
    out.reserve(registry_.size());
    for (const auto& [name, _] : registry_) {
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    return out;
}

const char* WorldProviderManager::defaultFormatName() {
    return "anvil";
}

} // namespace cursestone::world::format
