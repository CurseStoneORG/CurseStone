// Event — base class for all server events. Players, command
// senders and plugins listen on HandlerLists. Mirrors pocketmine
// /event/Event.php. (No plugin API yet — handler dispatch is internal.)
#pragma once
#include <string>
namespace cenisyspe::event {
class Event {
public:
    virtual ~Event() = default;
    virtual const std::string& eventName() const = 0;
};
} // namespace
