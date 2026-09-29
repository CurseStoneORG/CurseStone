// BlockBurnEvent — block event.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
#include "math/Vector3.h"
#include "main.h"   // math::Vector3 alias (cursestone::math::Vector3 = mcp_vector3_t)
namespace cursestone { class Player; }
namespace cursestone::event::block {
class BlockBurnEvent : public Event, public Cancellable {
public:
    cursestone::Player* player;
    math::Vector3 pos;
    int block_id;
    BlockBurnEvent(cursestone::Player* p, math::Vector3 v, int id=0) : player(p), pos(v), block_id(id) {}
    const std::string& eventName() const override {
        static std::string n = "BlockBurnEvent"; return n;
    }
};
} // namespace
