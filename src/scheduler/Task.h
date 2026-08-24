// Task — base class for scheduler tasks. C++.
// Subclasses override onRun() (called on the main thread every
// `period` ticks) or, for AsyncTask, onRun() runs on a worker
// thread and onCompletion() on the main thread.
#pragma once
#include <cstdint>
#include <atomic>
namespace atomikpe::scheduler {
class TaskHandler;
class Task {
public:
    virtual ~Task() = default;
    virtual void onRun() {}
    virtual void onCompletion() {}
    int  period() const { return period_; }
    void setPeriod(int p)  { period_ = p; }
    int delay() const { return delay_; }
    void setDelay(int d) { delay_ = d; }
    uint64_t nextRun() const { return next_run_; }
    void setNextRun(uint64_t tick) { next_run_ = tick; }
    bool isCancelled() const { return cancelled_; }
    void cancel() { cancelled_ = true; }
private:
    friend class TaskHandler;
    int period_ = 0;   // ticks between runs; 0 = one-shot
    int delay_ = 0;
    uint64_t next_run_ = 0;
    bool cancelled_ = false;
};

class AsyncTask : public Task {
public:
    // Worker-thread entry. Subclasses do the work here.
    void onRun() override {}
    // Main-thread callback after onRun() completes.
    void onCompletion() override {}
    // Result placeholder; subclasses can store arbitrary data.
    void* result() const { return result_; }
    void setResult(void* p) { result_ = p; }
private:
    void* result_ = nullptr;
};
} // namespace
