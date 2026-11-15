/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later

// NativePlugin — a plugin shipped as a shared library (.so on
// Linux/Android/macOS, .dll on Windows) that implements the C ABI from
// NativeAPI.h. The manifest is the same plugin.yml the Lua loader uses;
// the `main` field names the library file instead of an entry script.
//
// LIFECYCLE: load() reads the manifest, opens the library (dlopen /
// LoadLibrary), validates the vtable ABI version and calls the plugin's
// onLoad with its CsPluginApi. enable()/disable()/dtor drive onEnable /
// onDisable / onUnload the same way LuaPlugin drives its hooks.
//
// SAFETY: the library is opened with RTLD_LOCAL so plugins stay symbol-
// isolated from each other; every plugin callback is wrapped in
// catch(...) so a throwing callback is logged instead of unwinding into
// the server. The ABI version check plus the reload-safe command bridge
// (see NativeCommand) protect against stale function pointers.
#pragma once
#include "NativeAPI.h"
#include "PluginBase.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace cursestone::plugin {

class NativePlugin : public PluginBase {
  public:
    // dir is the plugin directory; dataRoot is the shared plugin-data directory.
    NativePlugin(std::string dir, std::string dataRoot);
    ~NativePlugin() override; // Disables, calls onUnload, closes the library.

    bool load() override;    // Manifest, dlopen/LoadLibrary, ABI check, onLoad.
    bool enable() override;  // Calls the onEnable hook when present.
    void disable() override; // Calls onDisable, removes listeners, cancels tasks.

    const std::string& name() const override {
        return name_;
    }
    const std::string& version() const override {
        return version_;
    }
    const std::string& author() const override {
        return author_;
    }
    bool isEnabled() const override {
        return enabled_;
    }
    const char* kind() const override {
        return "native";
    }

    /** @brief The per-plugin data folder (data_pl/<plugin>), shared with the data.* API. */
    const std::string& dataDir() const {
        return dataDir_;
    }

    /** @brief A live command registration handed to NativeCommand. */
    struct CommandEntry {
        CsCommandFn fn;
        void* ud;
    };

    /** @brief Registers a callback slot and returns its id for NativeCommand. */
    uint64_t addCommandEntry(CsCommandFn fn, void* ud);

    /** @brief The live registration for @p id, or nullptr after a reload replaced the plugin. */
    const CommandEntry* commandEntry(uint64_t id) const;

    /** @brief Schedules @p fn on the main thread; one-shot when @p repeat is false. */
    uint64_t scheduleTask(CsTaskFn fn, void* ud, int ticks, bool repeat);

    /** @brief Cancels a pending task scheduled by scheduleTask(). */
    void cancelTask(uint64_t id);

    /** @brief Runs @p fn under catch(...), logging a crash as a plugin error. */
    static void invokeGuarded(CsTaskFn fn, void* ud, const std::string& pluginName);

    /** @brief Runs a command callback under catch(...) with the same protection. */
    static void invokeCommand(CsCommandFn fn, const CsCommandSender* sender, int argc,
                              const char* const* argv, void* ud, const std::string& pluginName);

  private:
    // Reads plugin.yml, validates api/name/main and resolves the library file.
    // Returns false when the manifest is missing, invalid or incompatible.
    bool loadManifest();

    // Opens the shared library and binds the entry vtable; false on any failure.
    bool openLibrary();

    // Creates the plugin data directory; false on failure.
    bool ensureDataDir();

    // Fills apiRoot_ with the server group pointers and this plugin's identity.
    void buildApiRoot();

    std::string dir_;        // Plugin directory.
    std::string folderName_; // Plugin folder name used in data_pl.
    std::string dataRoot_;   // Shared plugin-data root.
    std::string dataDir_;    // Plugin data directory.
    std::string resourcesDir_; // Optional plugin resources directory.
    std::string name_;
    std::string version_ = "?";
    std::string author_;
    std::string api_;        // API version declared in plugin.yml.
    std::string libRel_;     // Library path from plugin.yml, plugin-relative.

    void* lib_ = nullptr;             // dlopen/LoadLibrary handle.
    const CsPluginVTable* vtable_ = nullptr;
    bool loadedOk_ = false;           // onLoad succeeded; onUnload may run.
    bool enabled_ = false;

    CsPluginApi apiRoot_{};
    std::unordered_map<uint64_t, CommandEntry> commands_;
    std::unordered_map<uint64_t, std::pair<CsTaskFn, void*>> tasks_;
    uint64_t nextCommandId_ = 1;
};

} // namespace cursestone::plugin
