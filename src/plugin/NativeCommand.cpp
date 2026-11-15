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

// NativeCommand — console-thread -> main-thread bridge implementation.
#include "NativeCommand.h"
#include "NativePlugin.h"
#include "PluginManager.h"
#include "scheduler/CallbackTask.h"
#include "scheduler/ServerScheduler.h"
#include <memory>

namespace cursestone::plugin {

NativeCommand::NativeCommand(NativePlugin* plugin, uint64_t regId, std::string name,
                             std::string description, std::string usage, PermMode mode,
                             std::string node)
    : Command(std::move(name), std::move(description), std::move(usage), ""), plugin_(plugin),
      regId_(regId), mode_(mode), node_(std::move(node)) {
}

// Same permission rules as LuaCommand::canUse — the two plugin kinds must
// not drift apart on who may run a command.
bool NativeCommand::canUse(CommandSender* sender) const {
    switch (mode_) {
    case PermMode::PUBLIC:
        return true;
    case PermMode::OP:
        return sender->isOp() || sender->hasPermission("cursestone.command." + name());
    case PermMode::NODE:
        return sender->hasPermission(node_) || sender->isOp();
    }
    return false;
}

bool NativeCommand::execute(CommandSender* sender, const std::string&,
                            const std::vector<std::string>& args) {
    // Copy the sender data now: when the command comes from the console
    // the sender is a CommandReader thread-local that does not survive
    // the queue hop. canUse() already validated permissions in dispatch().
    std::string senderName = sender->getName();
    bool isConsole = (senderName == "CONSOLE");
    bool isOp = sender->isOp();
    std::string pluginName = plugin_->name();
    const uint64_t regId = regId_;

    // A disabled plugin must not run callbacks; the queued task checks
    // the live instance again anyway (reload-safe, see the class comment).
    if (!plugin_->isEnabled()) {
        sender->sendMessage("That plugin is disabled.");
        return true;
    }

    auto task = std::make_shared<scheduler::CallbackTask>(
        [pluginName, regId, senderName, isConsole, isOp, args]() {
            PluginBase* base = PluginManager::get().find(pluginName);
            NativePlugin* plugin = dynamic_cast<NativePlugin*>(base);
            if (!plugin || !plugin->isEnabled())
                return;
            const NativePlugin::CommandEntry* entry = plugin->commandEntry(regId);
            if (!entry || !entry->fn)
                return; // Registration gone (plugin re-created): never call a dead pointer.

            CsCommandSender csSender;
            csSender.name = senderName.c_str();
            csSender.isConsole = isConsole;
            csSender.isOp = isOp;

            // argv points into `args`, which this lambda owns and which
            // outlives the plugin callback; documented lifetime holds.
            std::vector<const char*> argv(args.size());
            for (size_t i = 0; i < args.size(); ++i)
                argv[i] = args[i].c_str();

            NativePlugin::invokeCommand(entry->fn, &csSender, (int)argv.size(), argv.data(),
                                        entry->ud, pluginName);
        });

    scheduler::ServerScheduler::get().scheduleSync(task, 0, 0);
    return true;
}

} // namespace cursestone::plugin
