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
// ServerScheduler — main-thread tick scheduler + async task dispatcher.
//
// scheduleSync() runs tasks on the main thread (ticked every 50ms).
// scheduleAsync() posts tasks to the Server's AsyncPool (worker threads)
// and collects their completion callbacks for execution on the main
// thread during the next tick.
//
// STABILITY NOTES (post fire-cascade incident — see BlockFire.cpp):
//   - tick() no longer runs any Task::onRun() while holding mtx_. It
//     snapshots the due handlers under the lock, releases the lock,
//     then runs them. This means a task that calls scheduleSync()
//     from inside its own onRun() (e.g. a repeating world-update task
//     that reschedules itself, or schedules a follow-up task) can no
//     longer deadlock against the scheduler's own tick — and newly
//     scheduled tasks are simply picked up on a later tick, not
//     executed immediately in an unbounded chain.
//   - A hard cap (MAX_TASKS_PER_TICK) limits how many due tasks run
//     in a single tick. Any remainder is deferred to the next tick.
//     This is what actually prevents a cascade (e.g. fire spread)
//     from freezing a single tick no matter how many tasks a chain
//     of onRun() calls schedules.
//   - The async completion queue is capped (MAX_PENDING_COMPLETIONS).
//     If a burst of AsyncTasks finishes faster than the main thread
//     can drain them, further completions are dropped with a logged
//     warning instead of growing the queue (and memory) without
//     bound.
#pragma once
#include "TaskHandler.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <deque>
#include <utility>
#include <cstdint>
namespace cursestone::scheduler {
class AsyncTask;
class ServerScheduler {
public:
    static ServerScheduler& get();
    uint64_t scheduleSync(std::shared_ptr<Task> task, int delay_ticks = 0, int period_ticks = 0);
    uint64_t scheduleAsync(std::shared_ptr<AsyncTask> task, int delay_ticks = 0);
    void tick(uint64_t current_tick);
    size_t size() const;

    // Cancel a previously scheduled task (sync or async) by the id
    // returned from scheduleSync()/scheduleAsync(). Safe to call from
    // the main thread at any time, including from inside a task's own
    // onRun(). For sync tasks not yet run, this prevents them from
    // ever running. For async tasks already handed to the AsyncPool,
    // this prevents onCompletion() from firing on the main thread and
    // (best-effort) skips onRun() if it hasn't started yet — but a
    // worker thread that already began onRun() will still finish it;
    // AsyncTask::onRun() implementations that do meaningful work
    // should keep checking isCancelled() themselves if they can bail
    // out early.
    bool cancel(uint64_t task_id);

    // Number of async completions dropped so far because the
    // completion queue was full. Diagnostic only.
    size_t droppedCompletions() const { return dropped_completions_.load(std::memory_order_relaxed); }

private:
    static constexpr size_t MAX_TASKS_PER_TICK = 64;
    static constexpr size_t MAX_PENDING_COMPLETIONS = 512;

    std::vector<std::unique_ptr<TaskHandler>> handlers_;
    // Task ids whose TaskHandler::tick() returned true (finished/
    // cancelled) during the current tick()'s run phase, collected
    // while handlers_ is unlocked so we know exactly what to erase
    // once we re-lock — instead of re-deriving "is this done?" from
    // task state after the fact, which can disagree with what
    // TaskHandler::tick() itself decided (e.g. for periodic tasks
    // whose nextRun() has already been advanced past current_tick).
    std::vector<uint64_t> finished_task_ids_;
    mutable std::mutex mtx_;
    std::atomic<uint64_t> next_id_{1};

    // Async completion queue — tasks that finished on worker threads
    // and need their onCompletion() called on the main thread.
    std::deque<std::shared_ptr<AsyncTask>> completion_queue_;
    mutable std::mutex completion_mtx_;
    std::atomic<size_t> dropped_completions_{0};

    // task_id -> AsyncTask, kept only long enough to support cancel()
    // before the task is done and removed from the completion path.
    // Guarded by completion_mtx_ (same lock domain as the completion
    // queue, since both are touched from the worker-thread lambda).
    std::vector<std::pair<uint64_t, std::shared_ptr<AsyncTask>>> in_flight_async_;
};
} // namespace
