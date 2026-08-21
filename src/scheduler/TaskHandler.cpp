#include "TaskHandler.h"
namespace mcppeserver::scheduler {
bool TaskHandler::tick(uint64_t current_tick) {
    if (!task_ || task_->isCancelled()) return true;
    if (task_->nextRun() > current_tick) return false;
    task_->onRun();
    if (task_->period() > 0) {
        task_->setNextRun(current_tick + task_->period());
        return false;
    }
    task_->onCompletion();
    return true;
}
} // namespace
