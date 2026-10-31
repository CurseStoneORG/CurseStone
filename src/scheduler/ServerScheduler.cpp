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
// ServerScheduler — main-thread tick scheduler + async task dispatcher.
//
// scheduleSync() runs tasks on the main thread (ticked every 50ms).
// scheduleAsync() posts tasks to the Server's AsyncPool (worker threads)
// and collects their completion callbacks for execution on the main
// thread during the next tick.
#include "ServerScheduler.h"
#include "AsyncPool.h"
#include "../Server.h"
#include "../utils/MainLogger.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <string>

namespace cursestone::scheduler {

ServerScheduler& ServerScheduler::get() {
    static ServerScheduler s;
    return s;
}

size_t ServerScheduler::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return handlers_.size();
}

uint64_t ServerScheduler::scheduleSync(std::shared_ptr<Task> task, int delay, int period) {
    if (!task)
        return 0;
    std::lock_guard<std::mutex> lk(mtx_);
    uint64_t id = next_id_++;
    auto h = std::make_unique<TaskHandler>(id, task);
    task->setDelay(delay);
    task->setPeriod(period);
    // CRITICAL FIX (kept from the original fire-cascade postmortem):
    // previously this was `setNextRun(0 + delay)`. The 0 was meant to
    // be "current tick" but the literal 0 meant: schedule the task to
    // run when the global tick counter == delay. Since the server is
    // usually already past tick #30 by the time flint-and-steel is
    // used, the condition `nextRun() > current_tick` in
    // TaskHandler::tick() was ALWAYS false, so fire-spread tasks ran
    // IMMEDIATELY on the next tick instead of waiting 30 ticks. That,
    // combined with the chunk-generation cascade, is what froze the
    // server: every fire tick spawned another immediate fire tick.
    uint64_t now = Server::getInstance().getCurrentTick();
    task->setNextRun(now + (uint64_t)(delay > 0 ? delay : 0));
    handlers_.push_back(std::move(h));
    return id;
}

uint64_t ServerScheduler::scheduleAsync(std::shared_ptr<AsyncTask> task, int delay) {
    if (!task)
        return 0;

    auto* pool = Server::getInstance().getAsyncPool();
    if (!pool) {
        // Fallback: no AsyncPool available — run synchronously instead
        // of silently dropping the work.
        return scheduleSync(std::static_pointer_cast<Task>(task), delay, 0);
    }

    uint64_t task_id = next_id_++;

    // Track this task as in-flight so cancel(task_id) can find it and
    // suppress onCompletion() even if the worker thread has already
    // finished onRun() by the time cancel() is called.
    {
        std::lock_guard<std::mutex> lk(completion_mtx_);
        in_flight_async_.emplace_back(task_id, task);
    }

    // FIX: the worker-thread lambda now:
    //   1. Re-checks isCancelled() right before doing any work, so a
    //      cancel() issued between scheduling and the worker actually
    //      picking it up is honored.
    //   2. Wraps onRun() in try/catch. Before this fix, an exception
    //      thrown from any AsyncTask::onRun() (e.g. a file-write
    //      failure, a bad NBT structure) was uncaught on a worker
    //      thread. In C++, an exception that escapes a thread's entry
    //      function calls std::terminate() — this took down the
    //      ENTIRE server process, not just the failing task.
    //   3. Only enqueues the completion (and only if not cancelled)
    //      so a cancelled task's onCompletion() never fires.
    pool->submit([this, task, task_id]() {
        // Runs on a WORKER THREAD.
        if (task->isCancelled()) {
            std::lock_guard<std::mutex> lk(completion_mtx_);
            in_flight_async_.erase(std::remove_if(in_flight_async_.begin(), in_flight_async_.end(),
                                                  [task_id](const auto& p) {
                                                      return p.first == task_id;
                                                  }),
                                   in_flight_async_.end());
            return;
        }

        try {
            task->onRun();
        } catch (const std::exception& e) {
            utils::MainLogger::get().warn(
                std::string("[Scheduler] AsyncTask threw during onRun() on a worker thread: ") +
                e.what() + " — task dropped, server kept running.");
            std::lock_guard<std::mutex> lk(completion_mtx_);
            in_flight_async_.erase(std::remove_if(in_flight_async_.begin(), in_flight_async_.end(),
                                                  [task_id](const auto& p) {
                                                      return p.first == task_id;
                                                  }),
                                   in_flight_async_.end());
            return;
        } catch (...) {
            utils::MainLogger::get().warn(
                "[Scheduler] AsyncTask threw a non-std::exception during onRun() on a worker thread — task dropped, server kept running.");
            std::lock_guard<std::mutex> lk(completion_mtx_);
            in_flight_async_.erase(std::remove_if(in_flight_async_.begin(), in_flight_async_.end(),
                                                  [task_id](const auto& p) {
                                                      return p.first == task_id;
                                                  }),
                                   in_flight_async_.end());
            return;
        }

        std::lock_guard<std::mutex> lk(completion_mtx_);
        in_flight_async_.erase(std::remove_if(in_flight_async_.begin(), in_flight_async_.end(),
                                              [task_id](const auto& p) {
                                                  return p.first == task_id;
                                              }),
                               in_flight_async_.end());

        if (task->isCancelled())
            return;

        // FIX: cap pending completions so a burst of finished async
        // tasks can't grow this queue (and the memory it holds)
        // without bound if the main thread falls behind draining it.
        // Dropping is logged and only affects onCompletion() — the
        // actual work in onRun() already ran to completion.
        if (completion_queue_.size() >= MAX_PENDING_COMPLETIONS) {
            dropped_completions_.fetch_add(1, std::memory_order_relaxed);
            utils::MainLogger::get().warn(
                "[Scheduler] Async completion queue full (" +
                std::to_string(MAX_PENDING_COMPLETIONS) +
                ") — dropping a completed task's onCompletion() callback.");
            return;
        }
        completion_queue_.push_back(task);
    });

    return task_id;
}

bool ServerScheduler::cancel(uint64_t task_id) {
    if (task_id == 0)
        return false;

    // Sync tasks: mark cancelled. TaskHandler::tick() checks
    // isCancelled() before running and removes the handler.
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& h : handlers_) {
            if (h->taskId() == task_id) {
                h->task()->cancel();
                return true;
            }
        }
    }

    // Async tasks: mark cancelled. If the worker hasn't started yet,
    // the isCancelled() check in the submitted lambda skips onRun()
    // entirely. If onRun() already finished and the completion is
    // already queued, remove it from the queue so onCompletion()
    // never fires on the main thread.
    {
        std::lock_guard<std::mutex> lk(completion_mtx_);
        for (auto& p : in_flight_async_) {
            if (p.first == task_id) {
                p.second->cancel();
                completion_queue_.erase(
                    std::remove(completion_queue_.begin(), completion_queue_.end(), p.second),
                    completion_queue_.end());
                return true;
            }
        }
    }
    return false;
}

void ServerScheduler::tick(uint64_t current_tick) {
    // --- Sync tasks ---
    // FIX: previously this iterated `handlers_` and called
    // TaskHandler::tick() (which runs Task::onRun()) WHILE HOLDING
    // mtx_. Since scheduleSync() also locks mtx_, any task that
    // scheduled another sync task from inside its own onRun() — e.g.
    // a repeating world-update task rescheduling a follow-up — would
    // deadlock against itself the moment std::mutex (non-recursive)
    // was used, or at best re-enter the same tick's handler list
    // while it was being mutated. Either way this was the mechanism
    // behind the fire-spread cascade freeze: every fire tick's onRun()
    // synchronously scheduled the next fire tick, unbounded, all
    // inside one call to tick().
    //
    // Fix: snapshot which handlers are due under the lock, run them
    // WITHOUT holding the lock (so onRun() is free to call
    // scheduleSync()/cancel() without deadlocking), then remove the
    // finished/cancelled ones under the lock again. A hard cap
    // (MAX_TASKS_PER_TICK) bounds how much work a single tick can do
    // even if a chain of tasks keeps scheduling more of itself — any
    // excess is simply picked up on a later tick instead of running
    // immediately, which is what actually prevents a cascade from
    // freezing the tick loop.
    std::vector<TaskHandler*> due;
    const std::chrono::steady_clock::time_point drain_start =
        std::chrono::steady_clock::now();
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (auto& h : handlers_) {
            if (due.size() >= MAX_TASKS_PER_TICK)
                break;
            if (!h->task() || h->task()->isCancelled())
                continue;
            if (h->task()->nextRun() <= current_tick)
                due.push_back(h.get());
        }
    }

    // Run outside the lock, so a task that calls scheduleSync()/
    // cancel() from inside its own onRun()/onCompletion() can't
    // deadlock against mtx_. TaskHandler::tick() itself already
    // catches exceptions from onRun()/onCompletion(), so a bad task
    // can't escape from here either. Its return value is the single
    // source of truth for "is this handler done" — true means
    // finished/cancelled/errored and should be removed, false means
    // it's a periodic task that's been rescheduled and stays.
    // finished_task_ids_ is touched here without mtx_ held. That's
    // safe ONLY because tick() itself is always called from a single
    // thread (the main tick loop) and is never re-entered concurrently
    // with itself — if that ever changes, this needs its own mutex.
    for (auto* h : due) {
        if (h->tick(current_tick)) {
            finished_task_ids_.push_back(h->taskId());
        }
        // Wall-clock budget: same contract as the task-count cap above.
        // Slow handlers (liquid settling, fire-style chains) defer their
        // remainder to later ticks instead of overrunning the tick loop,
        // which is what keeps chunk streaming responsive while a world
        // loads.
        if (std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - drain_start)
                .count() >= SYNC_DRAIN_BUDGET_MS) {
            break;
        }
    }

    // Remove exactly the handlers TaskHandler::tick() told us are
    // done, plus anything cancelled asynchronously (e.g. via
    // cancel() called from another task's onRun() during this same
    // batch) that never got a chance to run this tick. Re-locking
    // here is safe and cheap — no user code (onRun/onCompletion) runs
    // while this lock is held.
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto& ids = finished_task_ids_;
        handlers_.erase(std::remove_if(handlers_.begin(), handlers_.end(),
                                       [&ids](const std::unique_ptr<TaskHandler>& h) {
                                           if (!h->task() || h->task()->isCancelled())
                                               return true;
                                           return std::find(ids.begin(), ids.end(), h->taskId()) !=
                                                  ids.end();
                                       }),
                        handlers_.end());
        ids.clear();
    }

    // --- Async completions ---
    // Drain the completion queue and run onCompletion() on the main
    // thread. Capped per tick (MAX_COMPLETIONS_PER_TICK) so a burst of
    // finished async work can't make a single tick run long; any
    // remainder is drained on subsequent ticks.
    {
        constexpr size_t MAX_COMPLETIONS_PER_TICK = 32;
        size_t processed = 0;
        std::vector<std::shared_ptr<AsyncTask>> batch;
        {
            std::lock_guard<std::mutex> lk(completion_mtx_);
            while (!completion_queue_.empty() && processed < MAX_COMPLETIONS_PER_TICK) {
                batch.push_back(std::move(completion_queue_.front()));
                completion_queue_.pop_front();
                ++processed;
            }
        }
        // Run onCompletion() outside completion_mtx_ so a completion
        // handler is free to call scheduleAsync()/cancel() again
        // without deadlocking on its own lock.
        for (auto& task : batch) {
            if (!task || task->isCancelled())
                continue;
            try {
                task->onCompletion();
            } catch (const std::exception& e) {
                utils::MainLogger::get().warn(
                    std::string(
                        "[Scheduler] AsyncTask threw during onCompletion() on the main thread: ") +
                    e.what());
            } catch (...) {
                utils::MainLogger::get().warn(
                    "[Scheduler] AsyncTask threw a non-std::exception during onCompletion() on the main thread.");
            }
        }
    }
}

} // namespace
