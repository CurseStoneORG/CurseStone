#pragma once
#include <thread>
#include <atomic>
#include <functional>
namespace mcppeserver::scheduler {
class AsyncWorker {
public:
    using Work = std::function<void()>;
    explicit AsyncWorker(Work w) : work_(std::move(w)) {}
    void start() { running_ = true; thread_ = std::thread([this]{ if (work_) work_(); }); }
    void join() { if (thread_.joinable()) thread_.join(); }
    void detach() { if (thread_.joinable()) thread_.detach(); }
private:
    Work work_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};
} // namespace
