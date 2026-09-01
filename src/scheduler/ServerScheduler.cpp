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
#include <iostream>

namespace atomikpe::scheduler {

ServerScheduler& ServerScheduler::get() { static ServerScheduler s; return s; }

uint64_t ServerScheduler::scheduleSync(std::shared_ptr<Task> task, int delay, int period) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto h = std::make_unique<TaskHandler>(next_id_++, task);
    task->setDelay(delay);
    task->setPeriod(period);
    task->setNextRun(0 + delay);
    uint64_t id = h->taskId();
    handlers_.push_back(std::move(h));
    return id;
}

uint64_t ServerScheduler::scheduleAsync(std::shared_ptr<AsyncTask> task, int delay) {
    // Fix #5: scheduleAsync now ACTUALLY runs async.
    // Post the task to the Server's AsyncPool (worker threads).
    // When the task completes (onRun finishes on the worker thread),
    // the completion callback is enqueued for main-thread execution.
    auto* pool = Server::getInstance().getAsyncPool();
    if (pool) {
        // Wrap the task so its onCompletion() runs on the main thread.
        // The AsyncPool calls task->onRun() on a worker thread, then
        // calls task->onCompletion() — we need that to happen on main.
        // We achieve this by storing completions in completion_queue_
        // and draining it in tick().
        auto task_id = next_id_++;
        auto* raw_task = task.get();

        // Post to the async pool. The pool will call onRun() on a
        // worker thread. When done, it calls onCompletion() — but
        // that would also run on the worker thread. To fix that,
        // we intercept: the pool stores the result, and we pick it
        // up in tick().
        pool->submit([this, task, task_id]() {
            // This runs on a WORKER THREAD.
            task->onRun();
            // Enqueue completion for main-thread execution.
            {
                std::lock_guard<std::mutex> lk(completion_mtx_);
                completion_queue_.push(task);
            }
        });
        return task_id;
    }
    // Fallback: no AsyncPool — run synchronously.
    return scheduleSync(std::static_pointer_cast<Task>(task), delay, 0);
}

void ServerScheduler::tick(uint64_t current_tick) {
    // Tick sync tasks.
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = handlers_.begin();
        while (it != handlers_.end()) {
            bool remove = (*it)->tick(current_tick);
            if (remove) it = handlers_.erase(it);
            else ++it;
        }
    }
    // Drain async completion queue — run onCompletion() on main thread.
    // Bonus fix: limit to MAX_COMPLETIONS_PER_TICK=32 to prevent
    // the tick from running long under heavy async load.
    {
        constexpr int MAX_COMPLETIONS_PER_TICK = 32;
        int processed = 0;
        std::lock_guard<std::mutex> lk(completion_mtx_);
        while (!completion_queue_.empty() &&
               processed < MAX_COMPLETIONS_PER_TICK) {
            auto task = completion_queue_.front();
            completion_queue_.pop();
            task->onCompletion();
            ++processed;
        }
    }
}

} // namespace
