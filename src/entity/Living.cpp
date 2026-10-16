/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */

#include "Living.h"
#include "event/EventManager.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"

namespace cursestone::entity {

bool Living::applyDamage(float amount) {
    bool was_alive = health_ > 0.0f;
    health_ -= amount;
    if (health_ < 0)
        health_ = 0;
    bool just_died = was_alive && health_ <= 0.0f;
    if (just_died)
        onDeath();
    else
        onHealthChanged(); // Immediately synchronize the health bar.
    return just_died;
}

DamageApply Living::damage(float amount) {
    if (isDead())
        return DamageApply::CANCELLED; // Prevent damage processing for dead entities.
    auto ev = event::EventManager::get().fire<event::entity::EntityDamageEvent>(
        this, amount, event::entity::DamageCause::CUSTOM);
    if (ev.isCancelled())
        return DamageApply::CANCELLED;
    return applyDamage(ev.amount) ? DamageApply::KILLED : DamageApply::HURT;
}

DamageApply Living::damage(float amount, event::entity::DamageCause cause) {
    if (isDead())
        return DamageApply::CANCELLED;
    auto ev =
        event::EventManager::get().fire<event::entity::EntityDamageEvent>(this, amount, cause);
    if (ev.isCancelled())
        return DamageApply::CANCELLED;
    return applyDamage(ev.amount) ? DamageApply::KILLED : DamageApply::HURT;
}

DamageApply Living::damageByEntity(float amount, Entity* damager,
                                   event::entity::DamageCause cause) {
    if (isDead())
        return DamageApply::CANCELLED;
    auto ev = event::EventManager::get().fire<event::entity::EntityDamageByEntityEvent>(
        this, amount, damager, cause);
    if (ev.isCancelled())
        return DamageApply::CANCELLED;
    return applyDamage(ev.amount) ? DamageApply::KILLED : DamageApply::HURT;
}

void Living::heal(float amount) {
    // Prevent dead entities from regenerating before respawning.
    // Natural regeneration could otherwise restore health in the lethal-hit tick.
    // This prevented respawn because isDead() would already return false.
    // Genisys and the reference server process regeneration only while the entity is alive.
    if (isDead())
        return;
    health_ += amount;
    if (health_ > max_health_)
        health_ = max_health_;
}

void Living::tick(uint64_t t) {
    Entity::tick(t);
    tickEffects();
}

// Manage potion effects using one slot per effect ID.
// Weaker effects are ignored; stronger effects replace existing ones.
// Instant effects apply immediately when added or replaced.
void Living::addEffect(std::shared_ptr<Effect> e) {
    if (!e || e->isExpired())
        return;
    uint8_t id = (uint8_t)e->id();
    for (auto& existing : effects_) {
        if ((uint8_t)existing->id() != id)
            continue;
        bool weaker = e->amplifier() < existing->amplifier() ||
                      (e->amplifier() == existing->amplifier() &&
                       e->duration() <= existing->duration());
        if (weaker)
            return;
        existing = std::move(e);
        onEffectModified(*existing);
        if (Effect::isInstant(id))
            applyEffectBehavior(*existing);
        return;
    }
    effects_.push_back(std::move(e));
    onEffectAdded(*effects_.back());
    if (Effect::isInstant(id))
        applyEffectBehavior(*effects_.back());
}

void Living::removeEffect(uint8_t id) {
    for (auto it = effects_.begin(); it != effects_.end(); ++it) {
        if ((uint8_t)(*it)->id() == id) {
            onEffectRemoved(**it);
            effects_.erase(it);
            return;
        }
    }
}

void Living::removeAllEffects() {
    // Snapshot effects because removal hooks may modify derived state.
    std::vector<std::shared_ptr<Effect>> snapshot = effects_;
    effects_.clear();
    for (auto& e : snapshot)
        onEffectRemoved(*e);
}

bool Living::hasEffect(uint8_t id) const {
    for (const auto& e : effects_)
        if ((uint8_t)e->id() == id)
            return true;
    return false;
}

const Effect* Living::getEffect(uint8_t id) const {
    for (const auto& e : effects_)
        if ((uint8_t)e->id() == id)
            return e.get();
    return nullptr;
}

// Apply an effect's gameplay behavior. Potion damage uses the MAGIC cause.
void Living::applyEffectBehavior(const Effect& e) {
    int level = e.amplifier() + 1;
    switch (e.id()) {
        case EFF_POISON:
            // Poison cannot reduce health below half a heart.
            if (health() > 1.0f)
                damage(1.0f, event::entity::DamageCause::MAGIC);
            break;
        case EFF_WITHER:
            damage(1.0f, event::entity::DamageCause::MAGIC);
            break;
        case EFF_REGENERATION:
            if (health() < maxHealth())
                heal(1.0f);
            break;
        case EFF_INSTANT_HEALTH:
            heal(4.0f * level);
            break;
        case EFF_INSTANT_DAMAGE:
            damage(6.0f * level, event::entity::DamageCause::MAGIC);
            break;
        case EFF_HUNGER:
            onHungerEffectTick(e.amplifier());
            break;
        case EFF_SATURATION:
            onSaturationEffectTick(e.amplifier());
            break;
        default:
            break;
    }
}

void Living::tickEffects() {
    if (effects_.empty())
        return;

    // Snapshot effect pointers because callbacks may modify the effect list.
    std::vector<Effect*> snapshot;
    snapshot.reserve(effects_.size());
    for (auto& e : effects_)
        snapshot.push_back(e.get());

    std::vector<uint8_t> expired;
    for (Effect* e : snapshot) {
        e->tick();
        if (e->isExpired()) {
            expired.push_back((uint8_t)e->id());
        } else if (e->canTick()) {
            applyEffectBehavior(*e);
        }
    }

    for (uint8_t id : expired)
        removeEffect(id);
}

} // namespace cursestone