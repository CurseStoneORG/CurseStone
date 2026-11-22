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
// AsyncPool — N worker threads that drain a thread-safe task queue.
// Chunk generation, file I/O and packet compression post tasks here.
// Mirrors scheduler/AsyncPool.
//
// Two lanes, one worker set: submit(work, priority=true) posts to a small
// fast lane that workers always drain before the FIFO lane. Short,
// latency-sensitive work (chunk encode transposition) rides the fast lane
// so a burst of queued generation tasks (a teleport or a fresh world can
// queue hundreds at once) can never push a client-facing encode behind
// seconds of terrain math; heavy batch work (generation, disk) rides the
// FIFO lane and keeps its submission order.
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
#include <chrono>
#include <exception>
#include <string>
#include "../utils/MainLogger.hpp"
#include "../utils/ThreadPort.h"
namespace cursestone::scheduler {

// Cumulative pool counters (all relaxed atomics; pure observers, they
// never influence scheduling). Snapshot + diff at two instants gives the
// queue wait / busy / idle numbers for that window — the spawn-pregen
// worker-scaling report reads exactly that.
struct PoolStats {
    std::atomic<uint64_t> submitted{0};      // tasks accepted (both lanes)
    std::atomic<uint64_t> completed{0};      // tasks run to completion
    std::atomic<uint64_t> queue_wait_ns{0};  // sum of (pop - enqueue) per task
    std::atomic<uint64_t> queue_wait_samples{0};
    std::atomic<uint64_t> queue_wait_max_ns{0};
    std::atomic<uint64_t> busy_ns{0};        // workers executing tasks
    std::atomic<uint64_t> idle_ns{0};        // workers waiting for work
    std::atomic<uint64_t> lock_wait_ns{0};   // submitters blocked on mtx_
    std::atomic<uint64_t> lock_waits{0};
    std::atomic<uint64_t> depth_max{0};      // queue high-water (both lanes)
};

class AsyncPool {
  public:
    explicit AsyncPool(int n_workers = 4) : n_(n_workers > 0 ? n_workers : 1) {
        for (int i = 0; i < n_; ++i)
            workers_.emplace_back([this, i] {
                mcp_set_thread_name((std::string("CS-Pool-") + std::to_string(i)).c_str());
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
    //
    // `priority` posts to the fast lane (see the class comment): short
    // client-facing work jumps ahead of queued heavy batch work. The FIFO
    // order WITHIN each lane is preserved.
    bool submit(Work w, bool priority = false) {
        const auto t0 = std::chrono::steady_clock::now();
        std::unique_lock<std::mutex> lk(mtx_);
        {
            const auto wait = std::chrono::steady_clock::now() - t0;
            stats_.lock_wait_ns.fetch_add(
                (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(wait)
                    .count(),
                std::memory_order_relaxed);
            stats_.lock_waits.fetch_add(1, std::memory_order_relaxed);
        }
        if (!running_)
            return false;
        const uint64_t enqueued = (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
        TimedWork tw;
        tw.work = std::move(w);
        tw.enqueue_ns = enqueued;
        (priority ? prio_ : queue_).push(std::move(tw));
        const size_t depth = prio_.size() + queue_.size();
        uint64_t seen = stats_.depth_max.load(std::memory_order_relaxed);
        while (seen < depth &&
               !stats_.depth_max.compare_exchange_weak(seen, depth,
                                                       std::memory_order_relaxed)) {
        }
        stats_.submitted.fetch_add(1, std::memory_order_relaxed);
        cv_.notify_one();
        return true;
    }

    // Point-in-time copy of the counters (single pass, no locks).
    struct Snapshot {
        uint64_t submitted = 0;
        uint64_t completed = 0;
        uint64_t queue_wait_ns = 0;
        uint64_t queue_wait_samples = 0;
        uint64_t queue_wait_max_ns = 0;
        uint64_t busy_ns = 0;
        uint64_t idle_ns = 0;
        uint64_t lock_wait_ns = 0;
        uint64_t lock_waits = 0;
        uint64_t depth_max = 0;
    };
    Snapshot statsSnapshot() const {
        Snapshot s;
        s.submitted = stats_.submitted.load(std::memory_order_relaxed);
        s.completed = stats_.completed.load(std::memory_order_relaxed);
        s.queue_wait_ns = stats_.queue_wait_ns.load(std::memory_order_relaxed);
        s.queue_wait_samples = stats_.queue_wait_samples.load(std::memory_order_relaxed);
        s.queue_wait_max_ns = stats_.queue_wait_max_ns.load(std::memory_order_relaxed);
        s.busy_ns = stats_.busy_ns.load(std::memory_order_relaxed);
        s.idle_ns = stats_.idle_ns.load(std::memory_order_relaxed);
        s.lock_wait_ns = stats_.lock_wait_ns.load(std::memory_order_relaxed);
        s.lock_waits = stats_.lock_waits.load(std::memory_order_relaxed);
        s.depth_max = stats_.depth_max.load(std::memory_order_relaxed);
        return s;
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

        // FIX: drain anything still left in the queues after workers
        // have exited, instead of destroying the queues (and silently
        // discarding pending work, e.g. an unflushed chunk write)
        // along with the pool. Run on the calling thread (normally
        // the main thread during shutdown) since no workers remain.
        // Each item still gets the same exception protection as the
        // normal worker path. The fast lane drains first, mirroring
        // the order workers used.
        std::queue<TimedWork> remaining;
        std::queue<TimedWork> remaining_prio;
        {
            std::unique_lock<std::mutex> lk(mtx_);
            std::swap(remaining_prio, prio_);
            std::swap(remaining, queue_);
        }
        while (!remaining_prio.empty()) {
            runOne(remaining_prio.front().work);
            remaining_prio.pop();
        }
        while (!remaining.empty()) {
            runOne(remaining.front().work);
            remaining.pop();
        }
    }

  private:
    // Queued item with its enqueue stamp, so a worker can charge the
    // queue wait to the right task. Timing only — never read for order.
    struct TimedWork {
        Work work;
        uint64_t enqueue_ns = 0;
    };

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

    static uint64_t nowNs() {
        return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }

    void workerLoop() {
        for (;;) {
            TimedWork tw;
            const uint64_t idle_start = nowNs();
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [&] {
                    return !running_ || !prio_.empty() || !queue_.empty();
                });
                if (!running_ && prio_.empty() && queue_.empty())
                    return;
                // Fast lane first: an encode waiting behind a generation
                // burst is exactly the teleport-fill stall this exists to
                // prevent. Within a lane, FIFO.
                if (!prio_.empty()) {
                    tw = std::move(prio_.front());
                    prio_.pop();
                } else {
                    tw = std::move(queue_.front());
                    queue_.pop();
                }
            }
            const uint64_t idle_ns = nowNs() - idle_start;
            stats_.idle_ns.fetch_add(idle_ns, std::memory_order_relaxed);
            if (tw.enqueue_ns != 0) {
                const uint64_t wait = nowNs() - tw.enqueue_ns;
                stats_.queue_wait_ns.fetch_add(wait, std::memory_order_relaxed);
                stats_.queue_wait_samples.fetch_add(1, std::memory_order_relaxed);
                uint64_t seen = stats_.queue_wait_max_ns.load(std::memory_order_relaxed);
                while (seen < wait &&
                       !stats_.queue_wait_max_ns.compare_exchange_weak(
                           seen, wait, std::memory_order_relaxed)) {
                }
            }
            const uint64_t busy_start = nowNs();
            runOne(tw.work);
            stats_.busy_ns.fetch_add(nowNs() - busy_start, std::memory_order_relaxed);
            stats_.completed.fetch_add(1, std::memory_order_relaxed);
        }
    }
    int n_;
    std::vector<std::thread> workers_;
    std::queue<TimedWork> prio_;   // fast lane: short client-facing work
    std::queue<TimedWork> queue_;  // FIFO lane: heavy batch work
    std::mutex mtx_;
    std::condition_variable cv_;
    bool running_{true}; // always accessed under mtx_
    PoolStats stats_;
};
} // namespace
