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

// NativeCommand — a command registered by a native plugin through
// CsCommandApi::add. PERMISSIONS mirror LuaCommand exactly:
//
//   PUBLIC ("default"/absent)   -> anyone
//   OP     ("op"/"admin")       -> ops, console, or cursestone.command.<name>
//   NODE   (any other string)   -> holders of the node, or an op
//
// THREADS: execute() may run on the CommandReader (console) thread. It
// copies the sender data, queues a CallbackTask through
// ServerScheduler::scheduleSync, and the callback runs next tick on the
// main thread. The task re-resolves the plugin by name and checks the
// command registration id against the LIVE plugin instance, so a
// /plugins reload that recreated the plugin can never call a function
// pointer from a library that was already dlclose()d.
#pragma once
#include "NativeAPI.h"
#include "command/Command.h"
#include <string>
#include <vector>

namespace cursestone::plugin {

class NativePlugin;

class NativeCommand : public Command {
  public:
    enum class PermMode { PUBLIC, OP, NODE };

    NativeCommand(NativePlugin* plugin, uint64_t regId, std::string name,
                  std::string description, std::string usage, PermMode mode, std::string node);

    // @copydoc LuaCommand::canUse
    bool canUse(CommandSender* sender) const override;
    // @copydoc Command::owner
    void* owner() const override {
        return plugin_;
    }
    bool execute(CommandSender* sender, const std::string& label,
                 const std::vector<std::string>& args) override;

  private:
    NativePlugin* plugin_;
    uint64_t regId_;
    PermMode mode_;
    std::string node_; // Only meaningful in NODE mode.
};

} // namespace cursestone::plugin
