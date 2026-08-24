// ThreadManager — owns the RakLib thread, AsyncPool worker threads,
// and CommandReader stdin thread. Provides join + clean-shutdown.
#pragma once
#include <thread>
#include <vector>
#include <memory>
namespace atomikpe {
class ThreadManager {
public:
    static ThreadManager& get();
    void registerThread(std::shared_ptr<std::thread> t) { threads_.push_back(std::move(t)); }
    void joinAll();
private:
    std::vector<std::shared_ptr<std::thread>> threads_;
};
} // namespace
