#include "Living.h"
namespace atomikpe::entity {
bool Living::damage(float amount) {
    health_ -= amount;
    if (health_ < 0) health_ = 0;
    return health_ <= 0;
}
void Living::heal(float amount) {
    health_ += amount;
    if (health_ > max_health_) health_ = max_health_;
}
void Living::tick(uint64_t t) {
    Entity::tick(t);
    for (auto& e : effects_) e->tick();
    // poison: 1 dmg every 2s
    // regen: +1 hp every 1.25s
    // (omitted for brevity; effect classes implement onTick)
}
} // namespace
