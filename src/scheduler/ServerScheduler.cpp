#include "ServerScheduler.h"
namespace atomikpe::scheduler {
ServerScheduler& ServerScheduler::get() { static ServerScheduler s; return s; }
uint64_t ServerScheduler::scheduleSync(std::shared_ptr<Task> task, int delay, int period) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto h = std::make_unique<TaskHandler>(next_id_++, task);
    task->setDelay(delay);
    task->setPeriod(period);
    // nextRun is set on first tick; we set it relative to current tick.
    task->setNextRun(0 + delay);
    uint64_t id = h->taskId();
    handlers_.push_back(std::move(h));
    return id;
}
uint64_t ServerScheduler::scheduleAsync(std::shared_ptr<AsyncTask> task, int delay) {
    // For now we run async tasks synchronously (real impl posts to AsyncPool).
    return scheduleSync(std::static_pointer_cast<Task>(task), delay, 0);
}
void ServerScheduler::tick(uint64_t current_tick) {
    std::lock_guard<std::mutex> lk(mtx_);
    auto it = handlers_.begin();
    while (it != handlers_.end()) {
        bool remove = (*it)->tick(current_tick);
        if (remove) it = handlers_.erase(it);
        else ++it;
    }
}
} // namespace
