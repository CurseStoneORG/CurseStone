// ChunkLoadEvent — level event.
#pragma once
#include "event/Event.h"
#include <string>
namespace atomikpe::level { class Level; }
namespace atomikpe::event::level {
class ChunkLoadEvent : public Event {
public:
    atomikpe::level::Level* level;
    int chunk_x = 0, chunk_z = 0;
    ChunkLoadEvent(atomikpe::level::Level* l = nullptr, int x=0, int z=0) : level(l), chunk_x(x), chunk_z(z) {}
    const std::string& eventName() const override {
        static std::string n = "ChunkLoadEvent"; return n;
    }
};
} // namespace
