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
// CommandReader — runs a background thread that reads stdin line by
// line. When a line is received, it forwards it to the Server's
// CommandMap. Mirrors pocketmine/command/CommandReader.
//
// CRITICAL: this uses a raw pthread + fgets() instead of std::cin
// because the async logger writes to stdout from a different thread.
// Mixing C stdio (fputs) with C++ streams (std::cin/std::cout) on
// the same FILE causes data corruption. Using fgets() on stdin keeps
// all I/O on the C stdio layer, which is thread-safe per-FILE.
#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <string>
namespace cursestone {
class CommandReader {
public:
    using LineCallback = std::function<void(const std::string&)>;
    explicit CommandReader(LineCallback cb) : cb_(std::move(cb)) {}
    ~CommandReader() { stop(); }
    void start() {
        running_ = true;
        thread_ = std::thread([this](){ this->loop(); });
    }
    void stop() {
        if (!running_.exchange(false)) return;
        // The thread may be blocked on fgets(stdin). We can't
        // interrupt fgets, so we detach and let it die when stdin
        // closes (process exit).
        if (thread_.joinable()) thread_.detach();
    }
private:
    void loop() {
        char buf[1024];
        while (running_.load()) {
            // Use fgets instead of std::getline to avoid mixing C and
            // C++ I/O on the same stream (async logger uses fputs).
            if (fgets(buf, sizeof(buf), stdin) == nullptr) {
                // EOF or error — stdin closed (likely during shutdown).
                break;
            }
            std::string line(buf);
            // Strip trailing newline.
            while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                line.pop_back();
            }
            if (!line.empty() && cb_) {
                cb_(line);
            }
        }
    }
    LineCallback cb_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
} // namespace
