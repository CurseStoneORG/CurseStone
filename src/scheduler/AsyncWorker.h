// AsyncWorker — a single one-off worker thread running a std::function.
//
// NOTE: nothing in the codebase currently uses this (AsyncPool is the
// path actually wired up via ServerScheduler::scheduleAsync()). It's
// kept for API compatibility / future use, but hardened so that IF
// something starts using it, it can't take the whole process down —
// same reasoning as AsyncPool::runOne(). Prefer AsyncPool for new
// code: unlike AsyncWorker, it bounds the number of concurrent
// threads instead of spawning a new OS thread per call.
#pragma once
#include <thread>
#include <atomic>
#include <functional>
#include <exception>
#include <string>
#include "../utils/MainLogger.hpp"
namespace cursestone::scheduler {
class AsyncWorker {
public:
    using Work = std::function<void()>;
    explicit AsyncWorker(Work w) : work_(std::move(w)) {}
    void start() {
        running_ = true;
        thread_ = std::thread([this]{
            // FIX: an exception escaping a std::thread's entry
            // function calls std::terminate() and kills the whole
            // process. Guard the same way AsyncPool does.
            try {
                if (work_) work_();
            } catch (const std::exception& e) {
                utils::MainLogger::get().warn(
                    std::string("[AsyncWorker] Task threw during execution: ") + e.what());
            } catch (...) {
                utils::MainLogger::get().warn(
                    "[AsyncWorker] Task threw a non-std::exception during execution.");
            }
            running_ = false;
        });
    }
    bool isRunning() const { return running_.load(std::memory_order_relaxed); }
    void join() { if (thread_.joinable()) thread_.join(); }
    void detach() { if (thread_.joinable()) thread_.detach(); }
private:
    Work work_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
} // namespace
