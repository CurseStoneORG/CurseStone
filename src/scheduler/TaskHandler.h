// TaskHandler — wraps a Task with scheduling metadata. The
// ServerScheduler owns a list of TaskHandlers and ticks them in
// order of nextRun.
#pragma once
#include "Task.h"
#include <memory>
namespace cenisyspe::scheduler {
class TaskHandler {
public:
    TaskHandler(uint64_t task_id, std::shared_ptr<Task> task)
        : task_id_(task_id), task_(std::move(task)) {}
    uint64_t taskId() const { return task_id_; }
    Task* task() const { return task_.get(); }
    // Called when the tick >= nextRun. Returns true if the task
    // should be removed (one-shot or cancelled).
    bool tick(uint64_t current_tick);
private:
    uint64_t task_id_;
    std::shared_ptr<Task> task_;
};
} // namespace
