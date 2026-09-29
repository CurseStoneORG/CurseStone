#include "Living.h"
#include "event/EventManager.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"

namespace cursestone::entity {

bool Living::applyDamage(float amount) {
    bool was_alive = health_ > 0.0f;
    health_ -= amount;
    if (health_ < 0) health_ = 0;
    bool just_died = was_alive && health_ <= 0.0f;
    if (just_died) onDeath();
    return just_died;
}

bool Living::damage(float amount) {
    if (isDead()) return false; // ya muerto — no re-disparar onDeath()
    auto ev = event::EventManager::get().fire<event::entity::EntityDamageEvent>(
        this, amount, event::entity::DamageCause::CUSTOM);
    if (ev.isCancelled()) return false;
    return applyDamage(ev.amount);
}

bool Living::damageByEntity(float amount, Entity* damager) {
    if (isDead()) return false;
    auto ev = event::EventManager::get().fire<event::entity::EntityDamageByEntityEvent>(
        this, amount, damager);
    if (ev.isCancelled()) return false;
    return applyDamage(ev.amount);
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
