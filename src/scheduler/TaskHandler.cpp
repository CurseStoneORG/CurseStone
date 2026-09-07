#include "TaskHandler.h"
#include "../utils/MainLogger.hpp"
#include <exception>
#include <string>
namespace atomikpe::scheduler {
bool TaskHandler::tick(uint64_t current_tick) {
    if (!task_ || task_->isCancelled()) return true;
    if (task_->nextRun() > current_tick) return false;

    // FIX: a task's onRun() must never be allowed to escape as an
    // uncaught exception. Before this fix, any exception thrown here
    // (e.g. a bad NBT write, a bounds error in a scheduled block
    // update) would propagate straight out of ServerScheduler::tick(),
    // out of Server::tick(), and crash the ENTIRE process — every
    // player, not just the one task. Catch, log, and treat the task
    // as failed (remove it) rather than let one bad task take down
    // the server.
    try {
        task_->onRun();
    } catch (const std::exception& e) {
        utils::MainLogger::get().warn(
            std::string("[Scheduler] Task threw during onRun(): ") + e.what() +
            " — task removed.");
        return true;
    } catch (...) {
        utils::MainLogger::get().warn(
            "[Scheduler] Task threw a non-std::exception during onRun() — task removed.");
        return true;
    }

    // Re-check cancellation: onRun() itself (or something it triggered
    // synchronously) may have cancelled this task.
    if (task_->isCancelled()) return true;

    if (task_->period() > 0) {
        task_->setNextRun(current_tick + task_->period());
        return false;
    }

    try {
        task_->onCompletion();
    } catch (const std::exception& e) {
        utils::MainLogger::get().warn(
            std::string("[Scheduler] Task threw during onCompletion(): ") + e.what());
    } catch (...) {
        utils::MainLogger::get().warn(
            "[Scheduler] Task threw a non-std::exception during onCompletion().");
    }
    return true;
}
} // namespace
