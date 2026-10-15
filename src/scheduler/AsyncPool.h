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
// AsyncPool — N worker threads that drain a thread-safe task queue.
// Chunk generation, file I/O and packet compression post tasks here.
// Mirrors pocketmine/scheduler/AsyncPool.
//
// STABILITY FIXES applied here (see also ServerScheduler.cpp, which
// already wraps its own scheduleAsync() lambdas in try/catch — the
// fixes below make the pool itself safe for ANY caller, not just
// ServerScheduler):
//   1. submit() after stop() is now rejected instead of silently
//      queuing work that will never run (and, before this fix, could
//      race with workers already past their exit check).
//   2. Every unit of work is run inside try/catch. Previously, an
//      exception thrown by `w()` would escape the worker thread's
//      entry point. In C++, that calls std::terminate() and kills
//      the ENTIRE process — not just the pool, not just the calling
//      player, everything. This matters even though
//      ServerScheduler::scheduleAsync() already catches internally,
//      because AsyncPool is a general-purpose primitive: nothing
//      about `submit()` guarantees every caller will remember to
//      wrap its own lambda.
//   3. `running_` is now consistently read/written under `mtx_`
//      everywhere (it already mostly was; `stop()` now also drains
//      any tasks left in the queue instead of abandoning them, so
//      shutdown never silently loses queued work — each is still run,
//      just synchronously on the calling thread if workers have
//      already been asked to stop, which only happens during a
//      deliberate stop()/destructor call).
#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include <exception>
#include <string>
#include "../utils/MainLogger.hpp"
namespace cursestone::scheduler {
class AsyncPool {
  public:
    explicit AsyncPool(int n_workers = 4) : n_(n_workers > 0 ? n_workers : 1) {
        for (int i = 0; i < n_; ++i)
            workers_.emplace_back([this] {
                this->workerLoop();
            });
    }
    ~AsyncPool() {
        stop();
    }

    using Work = std::function<void()>;

    // Returns false (and does not queue) if the pool is already
    // stopping/stopped. Callers that ignore the return value simply
    // lose that one unit of work, which is safe — it's the same
    // outcome as scheduleAsync()'s existing "no AsyncPool" fallback
    // path, just surfaced with a return value instead of happening
    // implicitly.
    bool submit(Work w) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (!running_)
            return false;
        queue_.push(std::move(w));
        cv_.notify_one();
        return true;
    }

    void stop() {
        {
            std::unique_lock<std::mutex> lk(mtx_);
            if (!running_)
                return; // already stopped — idempotent
            running_ = false;
        }
        cv_.notify_all();
        for (auto& t : workers_)
            if (t.joinable())
                t.join();

        // FIX: drain anything still left in the queue after workers
        // have exited, instead of destroying the queue (and silently
        // discarding pending work, e.g. an unflushed chunk write)
        // along with the pool. Run on the calling thread (normally
        // the main thread during shutdown) since no workers remain.
        // Each item still gets the same exception protection as the
        // normal worker path.
        std::queue<Work> remaining;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            std::swap(remaining, queue_);
        }
        while (!remaining.empty()) {
            runOne(remaining.front());
            remaining.pop();
        }
    }

  private:
    static void runOne(Work& w) {
        if (!w)
            return;
        try {
            w();
        } catch (const std::exception& e) {
            utils::MainLogger::get().warn(std::string("[AsyncPool] Task threw during execution: ") +
                                          e.what() + " — task dropped, pool kept running.");
        } catch (...) {
            utils::MainLogger::get().warn(
                "[AsyncPool] Task threw a non-std::exception during execution — task dropped, pool kept running.");
        }
    }

    void workerLoop() {
        for (;;) {
            Work w;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [&] {
                    return !running_ || !queue_.empty();
                });
                if (!running_ && queue_.empty())
                    return;
                if (queue_.empty())
                    continue; // spurious wake / stop with items still draining elsewhere
                w = std::move(queue_.front());
                queue_.pop();
            }
            runOne(w);
        }
    }
    int n_;
    std::vector<std::thread> workers_;
    std::queue<Work> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool running_{true}; // always accessed under mtx_
};
} // namespace
