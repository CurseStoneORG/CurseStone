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
// WorldProviderManager — implementation.
//
// The reference server's WorldProviderManager is essentially a static map; we
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

bool WorldProviderManager::registerProvider(const std::string& name, ProviderFactory factory) {
    if (!factory)
        return false;
    return registry_.emplace(name, std::move(factory)).second;
}

ProviderFactory WorldProviderManager::getFactory(const std::string& name) const {
    auto it = registry_.find(name);
    if (it == registry_.end())
        return nullptr;
    return it->second;
}

std::unique_ptr<WorldProvider> WorldProviderManager::create(const std::string& name) const {
    auto factory = getFactory(name);
    if (!factory)
        return nullptr;
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
