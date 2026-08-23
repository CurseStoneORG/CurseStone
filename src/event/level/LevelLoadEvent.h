// LevelLoadEvent — level event.
#pragma once
#include "Event.h"
#include <string>
namespace cenisyspe::level { class Level; }
namespace cenisyspe::event::level {
class LevelLoadEvent : public Event {
public:
    cenisyspe::level::Level* level;
    int chunk_x = 0, chunk_z = 0;
    LevelLoadEvent(cenisyspe::level::Level* l = nullptr, int x=0, int z=0) : level(l), chunk_x(x), chunk_z(z) {}
    const std::string& eventName() const override {
        static std::string n = "LevelLoadEvent"; return n;
    }
};
} // namespace
