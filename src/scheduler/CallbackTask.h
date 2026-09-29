// CallbackTask — wraps a std::function<void()> as a Task. Used
// by the Server to defer one-shot work onto the main thread.
#pragma once
#include "Task.h"
#include <functional>
namespace cursestone::scheduler {
class CallbackTask : public Task {
public:
    explicit CallbackTask(std::function<void()> fn) : fn_(std::move(fn)) {}
    void onRun() override { if (fn_) fn_(); }
private:
    std::function<void()> fn_;
};
} // namespace
