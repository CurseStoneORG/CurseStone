/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
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
#include "SaveCommand.h"
#include "Server.h"
#include "world/World.h"
#include "world/performance/SaveProfiler.h"
namespace cursestone {
bool SaveCommand::execute(CommandSender* sender, const std::string&,
                          const std::vector<std::string>& args) {
    // FASE L: the manual save runs through the budgeted drain pipeline -
    // this command only STARTS it and returns at once. Serializing and
    // writing thousands of chunks here (the old lvl->save()) held the
    // main tick thread for the whole run: TPS dropped to 0, the console
    // dispatch queue (which runs on the tick thread) stopped answering,
    // and the game froze until the last chunk landed.
    //
    // Optional argument: "diag" prints the save-pipeline diagnostics
    // (SaveProfiler window) instead of starting a save.
    if (!args.empty() && args.front() == "diag") {
        const auto sp = performance::SaveProfiler::peek();
        auto msOf = [&sp](performance::SaveStage s) {
            const auto& e = sp.stages[(size_t)s];
            return e.calls ? std::to_string(e.ns / 1000000) + " ms x" +
                                 std::to_string(e.calls)
                           : std::string("none");
        };
        sender->sendMessage("Save pipeline diagnostics (since boot):");
        sender->sendMessage(" snapshots: " + msOf(performance::SaveStage::Snapshot));
        sender->sendMessage(" db writes: " + msOf(performance::SaveStage::WorkerPut));
        sender->sendMessage(" table flushes: " + msOf(performance::SaveStage::EngineFlush));
        sender->sendMessage(" compactions: " + msOf(performance::SaveStage::EngineCompact));
        sender->sendMessage(" drain waits: " + msOf(performance::SaveStage::DrainWait));
        sender->sendMessage(" queue: " + std::to_string(sp.queue_depth) + " pending, max " +
                            std::to_string(sp.queue_depth_max) + " (" +
                            std::to_string((unsigned long long)(sp.queue_bytes / 1024)) +
                            " KB held, backpressure blocks " +
                            std::to_string(sp.enqueue_blocks) + ")");
        sender->sendMessage(" written: " + std::to_string(sp.chunks_written) +
                            " chunks, " + std::to_string((unsigned long long)(sp.wal_bytes / 1024)) +
                            " KB WAL, " + std::to_string((unsigned long long)(sp.table_bytes / 1024)) +
                            " KB tables, " + std::to_string(sp.write_failures) + " failures");
        return true;
    }

    Server& srv = Server::getInstance();
    auto* lvl = srv.getDefaultWorld();
    if (!lvl) {
        sender->sendMessage("No world to save");
        return true;
    }
    srv.requestFullSave(sender->getName());
    return true;
}
} // namespace
