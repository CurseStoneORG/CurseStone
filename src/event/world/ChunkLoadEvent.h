// ChunkLoadEvent — world event.
#pragma once
#include "event/Event.h"
#include <string>
namespace cursestone::world { class World; }
namespace cursestone::event::world {
class ChunkLoadEvent : public Event {
public:
    cursestone::world::World* world;
    int chunk_x = 0, chunk_z = 0;
    ChunkLoadEvent(cursestone::world::World* l = nullptr, int x=0, int z=0) : world(l), chunk_x(x), chunk_z(z) {}
    const std::string& eventName() const override {
        static std::string n = "ChunkLoadEvent"; return n;
    }
};
} // namespace
