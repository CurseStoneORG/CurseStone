// ServerScheduler — main-thread tick scheduler + async task dispatcher.
//
// scheduleSync() runs tasks on the main thread (ticked every 50ms).
// scheduleAsync() posts tasks to the Server's AsyncPool (worker threads)
// and collects their completion callbacks for execution on the main
// thread during the next tick.
#pragma once
#include "TaskHandler.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <queue>
namespace atomikpe::scheduler {
class AsyncTask;
class ServerScheduler {
public:
    static ServerScheduler& get();
    uint64_t scheduleSync(std::shared_ptr<Task> task, int delay_ticks = 0, int period_ticks = 0);
    uint64_t scheduleAsync(std::shared_ptr<AsyncTask> task, int delay_ticks = 0);
    void tick(uint64_t current_tick);
    size_t size() const { return handlers_.size(); }
private:
    std::vector<std::unique_ptr<TaskHandler>> handlers_;
    mutable std::mutex mtx_;
    std::atomic<uint64_t> next_id_{1};
    // Async completion queue — tasks that finished on worker threads
    // and need their onCompletion() called on the main thread.
    std::queue<std::shared_ptr<AsyncTask>> completion_queue_;
    mutable std::mutex completion_mtx_;
};
} // namespace
