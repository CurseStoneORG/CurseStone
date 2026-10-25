/*
 _____                     _____ _
/  __ \                   /  ___| |
| |  //_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_| |___/\___\____/ \__\___/|_| |_|\___/

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
// CRITICAL: stdin is kept off C stdio on every platform. fgets() holds
// the stdin FILE lock for the whole call, including the blocking read —
// a reader blocked on an idle terminal keeps that lock forever, and
// glibc's exit-time _IO_cleanup then deadlocks trying to lock every
// open FILE, so the process never actually leaves exit() after
// "Shutdown complete". Mixing C stdio (fputs) with C++ streams on the
// same FILE also causes data corruption. POSIX reads fd 0 with
// poll()+read(); Windows mirrors the same 200 ms cadence on the raw
// stdin handle (console handles are waitable, pipes need PeekNamedPipe),
// so stop() stays responsive and no FILE lock is held while idle.
#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif
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
        // Both loops wake every 200 ms and re-check the flag, so the
        // thread is gone shortly after the flag drops.
        if (thread_.joinable())
            thread_.join();
    }

  private:
#ifdef _WIN32
    void loop() {
        std::string pending;
        char buf[1024];
        HANDLE in = ::GetStdHandle(STD_INPUT_HANDLE);
        if (!in || in == INVALID_HANDLE_VALUE)
            return;
        // FILE_TYPE_REMOTE is ORed in for network shares; strip it so
        // remote disk handles fall into the plain-file branch.
        const DWORD type = ::GetFileType(in) & ~FILE_TYPE_REMOTE;
        while (running_.load()) {
            bool ready;
            if (type == FILE_TYPE_PIPE) {
                // Anonymous pipes are not waitable, so peek instead.
                DWORD avail = 0;
                if (!::PeekNamedPipe(in, nullptr, 0, nullptr, &avail, nullptr))
                    break; // stdin closed or broken.
                if (avail == 0) {
                    ::Sleep(200); // Same cadence as the wait branches.
                    continue;
                }
                ready = true;
            } else if (type == FILE_TYPE_CHAR) {
                // Console input handles are waitable; a timed wait keeps
                // the loop free of any held lock.
                ready = ::WaitForSingleObject(in, 200) == WAIT_OBJECT_0;
            } else {
                ready = true; // Regular files: ReadFile returns promptly.
            }
            if (!ready)
                continue; // Nothing yet — re-check the flag.
            DWORD n = 0;
            if (!::ReadFile(in, buf, (DWORD) sizeof(buf), &n, nullptr) || n == 0)
                break; // EOF or error — stdin closed.
            pending.append(buf, (size_t) n);
            dispatchLines(pending);
        }
    }
#else
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
            pending.append(buf, (size_t) n);
            dispatchLines(pending);
        }
    }
#endif
    // Forwards complete '\n'-terminated lines and keeps the partial
    // tail for the next read.
    void dispatchLines(std::string& pending) {
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
    LineCallback cb_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
} // namespace
