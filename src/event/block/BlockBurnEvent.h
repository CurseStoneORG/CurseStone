// BlockBurnEvent — block event.
#pragma once
#include "Event.h"
#include "Cancellable.h"
#include "math/Vector3.h"
namespace mcppeserver { class Player; }
namespace mcppeserver::event::block {
class BlockBurnEvent : public Event, public Cancellable {
public:
    mcppeserver::Player* player;
    math::Vector3 pos;
    int block_id;
    BlockBurnEvent(mcppeserver::Player* p, math::Vector3 v, int id=0) : player(p), pos(v), block_id(id) {}
    const std::string& eventName() const override {
        static std::string n = "BlockBurnEvent"; return n;
    }
};
} // namespace
