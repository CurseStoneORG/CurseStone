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
// Task — base class for scheduler tasks. C++.
// Subclasses override onRun() (called on the main thread every
// `period` ticks) or, for AsyncTask, onRun() runs on a worker
// thread and onCompletion() on the main thread.
//
// THREAD SAFETY NOTE: `cancelled_` is read from the main thread
// (TaskHandler::tick / ServerScheduler completion drain) and, for
// AsyncTask, may also be checked from a worker thread right before
// onRun() starts. It's declared std::atomic<bool> so both sides see
// a consistent value without needing a mutex for this single flag.
#pragma once
#include <cstdint>
#include <atomic>
namespace cursestone::scheduler {
class TaskHandler;
class Task {
  public:
    virtual ~Task() = default;
    virtual void onRun() {
    }
    virtual void onCompletion() {
    }
    int period() const {
        return period_;
    }
    void setPeriod(int p) {
        period_ = p;
    }
    int delay() const {
        return delay_;
    }
    void setDelay(int d) {
        delay_ = d;
    }
    uint64_t nextRun() const {
        return next_run_;
    }
    void setNextRun(uint64_t tick) {
        next_run_ = tick;
    }
    bool isCancelled() const {
        return cancelled_.load(std::memory_order_acquire);
    }
    void cancel() {
        cancelled_.store(true, std::memory_order_release);
    }

  private:
    friend class TaskHandler;
    int period_ = 0; // ticks between runs; 0 = one-shot
    int delay_ = 0;
    uint64_t next_run_ = 0;
    std::atomic<bool> cancelled_{false};
};

class AsyncTask : public Task {
  public:
    // Worker-thread entry. Subclasses do the work here.
    void onRun() override {
    }
    // Main-thread callback after onRun() completes.
    void onCompletion() override {
    }
    // Result placeholder; subclasses can store arbitrary data.
    void* result() const {
        return result_;
    }
    void setResult(void* p) {
        result_ = p;
    }

  private:
    void* result_ = nullptr;
};
} // namespace
