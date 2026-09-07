// BlockBreakEvent — block event.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
#include "math/Vector3.h"
#include "main.h"   // math::Vector3 alias (atomikpe::math::Vector3 = mcp_vector3_t)
namespace atomikpe { class Player; }
namespace atomikpe::event::block {
class BlockBreakEvent : public Event, public Cancellable {
public:
    atomikpe::Player* player;
    math::Vector3 pos;
    int block_id;
    BlockBreakEvent(atomikpe::Player* p, math::Vector3 v, int id=0) : player(p), pos(v), block_id(id) {}
    const std::string& eventName() const override {
        static std::string n = "BlockBreakEvent"; return n;
    }
};
} // namespace
