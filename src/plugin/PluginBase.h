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

// PluginBase — the language-agnostic plugin contract PluginManager
// holds. LuaPlugin and NativePlugin both implement it, so the manager
// scans, enables, disables and lists them through one vector without
// knowing which runtime backs each entry.
//
// LIFECYCLE (same order for every backend):
//   load()   -> manifest + entry point ready; false discards the plugin.
//   enable() -> onEnable hook; false disables and discards.
//   disable()-> onDisable hook + unlisten + task cancel (idempotent).
//   ~Plugin  -> unload; dtors must clean up even after a failed load().
#pragma once
#include <string>

namespace cursestone::plugin {

class PluginBase {
  public:
    virtual ~PluginBase() = default;

    /** @brief Loads manifest and entry point; false discards the plugin. */
    virtual bool load() = 0;

    /** @brief Runs the enable hook; false disables and discards. */
    virtual bool enable() = 0;

    /** @brief Runs the disable hook and detaches listeners, tasks and commands. */
    virtual void disable() = 0;

    /** @brief Plugin identity from plugin.yml; empty until load() succeeds. */
    virtual const std::string& name() const = 0;
    virtual const std::string& version() const = 0;
    virtual const std::string& author() const = 0;

    /** @brief True between successful enable() and disable(). */
    virtual bool isEnabled() const = 0;

    /** @brief Backend tag for /plugins: "lua" or "native". */
    virtual const char* kind() const = 0;
};

} // namespace cursestone::plugin
