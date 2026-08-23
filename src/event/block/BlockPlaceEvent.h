// BlockPlaceEvent — block event.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
#include "math/Vector3.h"
namespace cenisyspe { class Player; }
namespace cenisyspe::event::block {
class BlockPlaceEvent : public Event, public Cancellable {
public:
    cenisyspe::Player* player;
    math::Vector3 pos;
    int block_id;
    BlockPlaceEvent(cenisyspe::Player* p, math::Vector3 v, int id=0) : player(p), pos(v), block_id(id) {}
    const std::string& eventName() const override {
        static std::string n = "BlockPlaceEvent"; return n;
    }
};
} // namespace
