// AsyncPool — N worker threads that drain a thread-safe task queue.
// Chunk generation, file I/O and packet compression post tasks here.
// Mirrors pocketmine/scheduler/AsyncPool.
#pragma once
#include <vector>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
namespace mcppeserver::scheduler {
class AsyncPool {
public:
    explicit AsyncPool(int n_workers = 4) : n_(n_workers) {
        for (int i = 0; i < n_; ++i)
            workers_.emplace_back([this]{ this->workerLoop(); });
    }
    ~AsyncPool() { stop(); }
    using Work = std::function<void()>;
    void submit(Work w) {
        std::unique_lock<std::mutex> lk(mtx_);
        queue_.push(std::move(w));
        cv_.notify_one();
    }
    void stop() {
        { std::unique_lock<std::mutex> lk(mtx_); running_ = false; }
        cv_.notify_all();
        for (auto& t : workers_) if (t.joinable()) t.detach();
    }
private:
    void workerLoop() {
        for (;;) {
            Work w;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [&] { return !running_ || !queue_.empty(); });
                if (!running_ && queue_.empty()) return;
                w = std::move(queue_.front());
                queue_.pop();
            }
            w();
        }
    }
    int n_;
    std::vector<std::thread> workers_;
    std::queue<Work> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> running_{true};
};
} // namespace
