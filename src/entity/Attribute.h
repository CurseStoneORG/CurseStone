// Attribute — server-side attribute like max_health, movement_speed,
// attack_damage, knockback_resistance. Sent in UpdateAttributesPacket.
#pragma once
#include <string>
namespace cenisyspe::entity {
class Attribute {
public:
    Attribute() = default;
    Attribute(std::string name, float min, float max, float value)
        : name_(std::move(name)), min_(min), max_(max), value_(value) {}
    const std::string& name() const { return name_; }
    float min() const { return min_; }
    float max() const { return max_; }
    float value() const { return value_; }
    void setValue(float v) { if (v >= min_ && v <= max_) value_ = v; }
private:
    std::string name_;
    float min_, max_, value_;
};
} // namespace
