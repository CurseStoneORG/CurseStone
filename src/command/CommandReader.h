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
// CommandMap. Mirrors command/CommandReader.
//
// CRITICAL: this reads fd 0 with poll()+read() instead of fgets() for
// two reasons. First, stdio fgets holds the stdin FILE lock for the
// whole call, including the blocking read — a detached reader blocked
// on an idle terminal keeps that lock forever, and glibc's exit-time
// _IO_cleanup then deadlocks trying to lock every open FILE, so the
// process never actually leaves exit() after "Shutdown complete".
// Second, mixing C stdio (fputs) with C++ streams on the same FILE
// causes data corruption; poll+read keeps stdin entirely off stdio.
#pragma once
#include <poll.h>
#include <unistd.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>
namespace cursestone {
class CommandReader {
  public:
    using LineCallback = std::function<void(const std::string&)>;
    explicit CommandReader(LineCallback cb) : cb_(std::move(cb)) {
    }
    ~CommandReader() {
        stop();
    }
    void start() {
        running_ = true;
        thread_ = std::thread([this]() {
            this->loop();
        });
    }
    void stop() {
        if (!running_.exchange(false))
            return;
        // The poll loop wakes every 200 ms and re-checks the flag, so
        // the thread is gone shortly after the flag drops.
        if (thread_.joinable())
            thread_.join();
    }

  private:
    void loop() {
        std::string pending;
        char buf[1024];
        while (running_.load()) {
            struct pollfd pfd {0, POLLIN, 0};
            int pr = ::poll(&pfd, 1, 200);
            if (pr <= 0)
                continue; // Timeout or interrupt — re-check the flag.
            ssize_t n = ::read(0, buf, sizeof(buf));
            if (n <= 0)
                break; // EOF or error — stdin closed.
            pending.append(buf, (size_t)n);
            size_t pos;
            while ((pos = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, pos);
                pending.erase(0, pos + 1);
                while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
                    line.pop_back();
                }
                if (!line.empty() && cb_) {
                    cb_(line);
                }
            }
        }
    }
    LineCallback cb_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
} // namespace
