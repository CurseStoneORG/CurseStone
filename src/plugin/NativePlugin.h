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

// NativePlugin — a plugin shipped as a BARE shared library file dropped
// directly into plugins/ (.so on Linux/Android/macOS, .dll on Windows):
// no folder, no plugin.yml. The library implements the C ABI from
// NativeAPI.h and its vtable descriptor (name / version / author / api)
// IS the manifest.
//
// IDENTITY: the file stem (HelloNative from HelloNative.so) owns the
// data_pl/<stem> folder and doubles as the fallback display name; the
// descriptor name wins for display when it differs. Uniqueness rules are
// enforced by PluginManager (duplicate stems and duplicate descriptor
// names both disable the copies involved).
//
// LIFECYCLE: load() opens the library (dlopen / LoadLibrary), validates
// the vtable ABI version, checks the descriptor's `api` claim against
// CURSESTONE_API and calls onLoad with the plugin's CsPluginApi.
// enable()/disable()/dtor drive onEnable / onDisable / onUnload the same
// way LuaPlugin drives its hooks.
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
    // libPath is the bare .so/.dll inside the plugins folder; dataRoot is
    // the shared plugin-data directory (data_pl).
    NativePlugin(std::string libPath, std::string dataRoot);
    ~NativePlugin() override; // Disables, calls onUnload, closes the library.

    bool load() override;    // dlopen/LoadLibrary, ABI check, desc identity, onLoad.
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

    /** @brief The per-plugin data folder (data_pl/<stem>), shared with the data.* API. */
    const std::string& dataDir() const {
        return dataDir_;
    }

    /** @brief The plugins-folder file backing this plugin, as scanned. */
    const std::string& libPath() const {
        return libPath_;
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
    // Opens the shared library and binds the entry vtable; false on any failure.
    bool openLibrary();

    // Creates the plugin data directory; false on failure.
    bool ensureDataDir();

    // Fills apiRoot_ with the server group pointers and this plugin's identity.
    void buildApiRoot();

    std::string libPath_;     // Bare library file inside plugins/.
    std::string stem_;        // File stem: data_pl key + fallback display name.
    std::string pluginsDir_;  // Absolute path of plugins/ (api.pluginDir).
    std::string dataRoot_;    // Shared plugin-data root.
    std::string dataDir_;     // Plugin data directory (data_pl/<stem>).
    std::string name_;        // Descriptor name once loaded; stem before that.
    std::string version_ = "?";
    std::string author_;

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
