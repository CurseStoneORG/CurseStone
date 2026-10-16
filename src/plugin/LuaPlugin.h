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
#pragma once
#include <lua.hpp>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <chrono>

namespace cursestone::plugin {

/// @brief Server-wide Lua execution limits. Zero disables a limit.
struct PluginLimits {
    int64_t instructions = 1000000; ///< Maximum VM instructions per outermost call.
    int64_t timeMs = 100;           ///< Maximum wall-clock time per outermost call.
    int maxErrors = 10;             ///< Consecutive failures before auto-disable.
};

class LuaPlugin {
  public:
    /// @brief Sets limits for subsequently loaded plugins.
    /// @note Main-thread only; call before PluginManager::loadAll().
    static void setLimits(const PluginLimits& limits);

    /// @brief Returns the active execution limits.
    static const PluginLimits& limits();

    // dir is the plugin directory; dataRoot is the shared plugin-data directory.
    LuaPlugin(std::string dir, std::string dataRoot);
    ~LuaPlugin(); // Closes the Lua state if present.

    bool load();    // Loads the manifest, state, and entry script.
    bool enable();  // Calls onEnable() if available.
    void disable(); // Calls onDisable(), removes listeners, cancels tasks, and disables the plugin.

    // Protected call for a function and nargs arguments already on the stack.
    // Logs errors and returns false while restoring the stack.
    bool pcall(int nargs, int nresults);

    // Retrieves the plugin associated with a Lua state.
    static LuaPlugin* from(lua_State* L);

    // Loads a plugin-relative Lua file with require-style caching.
    // Returns the chunk result; each file executes only once.
    int include(lua_State* L);

    // Imports modules from src/ without requiring the .lua extension.
    // Supports dot-separated paths and shares the include() cache.
    int import(lua_State* L);

    /// @brief Raises a Lua error when a VM execution limit is exceeded.
    void checkLimits(lua_State* L);

    // Manages scheduler task references.
    void addTask(uint64_t id, int ref);
    void cancelTask(uint64_t id);  // Cancels the scheduled task and releases its Lua reference.
    void releaseTask(uint64_t id); // Releases the Lua reference for a completed one-shot task.
    void cancelAllTasks();

    lua_State* L() const {
        return L_;
    }
    const std::string& name() const {
        return name_;
    }
    const std::string& version() const {
        return version_;
    }
    const std::string& author() const {
        return author_;
    }
    const std::string& api() const {
        return api_;
    }
    const std::string& entry() const {
        return entry_;
    }

    // Returns the plugin data directory; access it only through sanitized data.* bindings.
    const std::string& dataDir() const {
        return dataDir_;
    }

    // Returns the optional resources directory used by plugin.getResource().
    const std::string& resourcesDir() const {
        return resourcesDir_;
    }
    bool isEnabled() const {
        return enabled_;
    }

  private:
    std::string dir_;          // Plugin directory.
    std::string folderName_;   // Plugin folder name used in data_pl.
    std::string dataRoot_;     // Shared plugin-data root.
    std::string dataDir_;      // Plugin data directory.
    std::string resourcesDir_; // Plugin resources directory.
    std::string name_;
    std::string version_ = "?";
    std::string author_;
    std::string api_;
    std::string entry_; // Relative entry-script path.
    lua_State* L_ = nullptr;
    int tableRef_ = LUA_NOREF; // Reference to the entry script's returned table.
    bool enabled_ = false;
    std::unordered_map<uint64_t, int> tasks_;
    std::unordered_map<std::string, int> included_; // Included module cache.

    int callDepth_ = 0;
    int hookCount_ = 1000;
    std::string tripMessage_;
    int consecutiveErrors_ = 0;
    int64_t instructionsUsed_ = 0;
    std::chrono::steady_clock::time_point callStart_;
    bool limitTripped_ = false;   // A limit was exceeded during the current outermost call.
    bool breakerTripped_ = false; // Too many consecutive errors; calls are blocked.
    bool inDisable_ = false;

    void installLimitHook();
    void tripBreaker();

    // Shared include/import implementation with require-style caching.
    int loadModule(lua_State* L, const std::string& rel, const char* what);

    // Reads plugin.yml, validates its fields and API, and resolves the entry script.
    // Returns false if the manifest is missing, invalid, or incompatible.
    bool loadManifest();

    // Creates the plugin data directory if needed; returns false on failure.
    bool ensureDataDir();
};

}