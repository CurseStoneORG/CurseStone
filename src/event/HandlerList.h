// HandlerList — per-event-type list of subscriber callbacks. Each
// Event subclass has a static HandlerList member. Mirrors pocketmine
// /event/HandlerList.php.
#pragma once
#include <functional>
#include <vector>
#include "Event.h"
namespace mcppeserver::event {
class HandlerList {
public:
    using Handler = std::function<void(const Event&)>;
    void registerHandler(Handler h) { handlers_.push_back(std::move(h)); }
    void dispatch(const Event& e) const {
        for (const auto& h : handlers_) h(e);
    }
    size_t size() const { return handlers_.size(); }
private:
    std::vector<Handler> handlers_;
};
} // namespace
