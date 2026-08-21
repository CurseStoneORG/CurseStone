// ServerScheduler — main-thread tick scheduler. Holds a list of
// TaskHandlers and ticks them every server tick (50ms). Mirrors
// pocketmine/scheduler/ServerScheduler.
#pragma once
#include "TaskHandler.h"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
namespace mcppeserver::scheduler {
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
};
} // namespace
