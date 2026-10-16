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
// WorldProviderManager — registry of on-disk world formats. C++.
//
// The reference server's WorldProviderManager is a static registry that maps a
// format identifier string (e.g. "anvil", "leveldb") to a concrete
// WorldProvider class. We mirror that pattern, but the C++ version is
// a non-static class so test code can inject a fresh registry per
// test.
//
// Concrete providers register themselves with the manager at process
// startup (see anvil/Anvil.cpp's static initializer). The World loader
// then asks the manager for a provider by format name; if absent it
// falls back to the default ("anvil").
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../CurseStone.h"
#include "WorldProvider.h"

namespace cursestone::world::format {

// Factory callback. Creates a fresh provider instance.
using ProviderFactory = std::function<std::unique_ptr<WorldProvider>()>;

class WorldProviderManager {
  public:
    static WorldProviderManager& getInstance();

    // Register a provider by canonical name. Names are lower-case
    // ASCII; the manager stores them as-is and matches case-sensitively.
    // Returns false if the name is already taken.
    bool registerProvider(const std::string& name, ProviderFactory factory);

    // Look up a factory by name. Returns nullptr if unknown.
    ProviderFactory getFactory(const std::string& name) const;

    // Convenience: instantiate a provider by name, returning nullptr
    // on unknown name. The returned pointer is owned by the caller.
    std::unique_ptr<WorldProvider> create(const std::string& name) const;

    // List every registered format name (sorted for stable output).
    std::vector<std::string> listFormats() const;

    // Default format used when a config doesn't specify one.
    static const char* defaultFormatName();

  private:
    WorldProviderManager() = default;
    std::unordered_map<std::string, ProviderFactory> registry_;
};

} // namespace cursestone::world::format
