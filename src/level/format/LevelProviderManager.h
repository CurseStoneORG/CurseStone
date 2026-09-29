// SPDX-License-Identifier: LGPL-3.0-or-later
// LevelProviderManager — registry of on-disk world formats. C++.
//
// PocketMine's LevelProviderManager is a static registry that maps a
// format identifier string (e.g. "anvil", "leveldb") to a concrete
// LevelProvider class. We mirror that pattern, but the C++ version is
// a non-static class so test code can inject a fresh registry per
// test.
//
// Concrete providers register themselves with the manager at process
// startup (see anvil/Anvil.cpp's static initializer). The Level loader
// then asks the manager for a provider by format name; if absent it
// falls back to the default ("anvil").
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "../../main.h"
#include "LevelProvider.h"

namespace cursestone::level::format {

// Factory callback. Creates a fresh provider instance.
using ProviderFactory = std::function<std::unique_ptr<LevelProvider>()>;

class LevelProviderManager {
public:
    static LevelProviderManager& getInstance();

    // Register a provider by canonical name. Names are lower-case
    // ASCII; the manager stores them as-is and matches case-sensitively.
    // Returns false if the name is already taken.
    bool registerProvider(const std::string& name, ProviderFactory factory);

    // Look up a factory by name. Returns nullptr if unknown.
    ProviderFactory getFactory(const std::string& name) const;

    // Convenience: instantiate a provider by name, returning nullptr
    // on unknown name. The returned pointer is owned by the caller.
    std::unique_ptr<LevelProvider> create(const std::string& name) const;

    // List every registered format name (sorted for stable output).
    std::vector<std::string> listFormats() const;

    // Default format used when a config doesn't specify one.
    static const char* defaultFormatName();

private:
    LevelProviderManager() = default;
    std::unordered_map<std::string, ProviderFactory> registry_;
};

} // namespace cursestone::level::format
