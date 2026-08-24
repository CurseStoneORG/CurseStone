// BlockBurnEvent — block event.
#pragma once
#include "Event.h"
#include <string>
#include "Cancellable.h"
#include "math/Vector3.h"
namespace atomikpe { class Player; }
namespace atomikpe::event::block {
class BlockBurnEvent : public Event, public Cancellable {
public:
    atomikpe::Player* player;
    math::Vector3 pos;
    int block_id;
    BlockBurnEvent(atomikpe::Player* p, math::Vector3 v, int id=0) : player(p), pos(v), block_id(id) {}
    const std::string& eventName() const override {
        static std::string n = "BlockBurnEvent"; return n;
    }
};
} // namespace
