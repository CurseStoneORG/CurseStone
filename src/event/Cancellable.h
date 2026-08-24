#pragma once
#include "Event.h"
namespace atomikpe::event {
class Cancellable {
public:
    virtual ~Cancellable() = default;
    bool isCancelled() const { return cancelled_; }
    void setCancelled(bool c = true) { cancelled_ = c; }
private:
    bool cancelled_ = false;
};
} // namespace
