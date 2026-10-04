// Location — Position + yaw + pitch. Mirrors pocketmine/level/Location.
#pragma once
#include "Position.h"

namespace cursestone::world {

class Location : public Position {
public:
    Location() = default;
    Location(double x, double y, double z, std::shared_ptr<World> l = nullptr,
             double yaw = 0.0, double pitch = 0.0)
        : Position(x, y, z, std::move(l)), yaw_(yaw), pitch_(pitch) {}

    double yaw()   const { return yaw_; }
    double pitch() const { return pitch_; }
    void setYaw(double y)   { yaw_ = y; }
    void setPitch(double p) { pitch_ = p; }

protected:
    double yaw_{0}, pitch_{0};
};

} // namespace cursestone::world
