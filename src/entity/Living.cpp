/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */

#include "Living.h"
#include "Player.h"
#include "Server.h"
#include "event/EventManager.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"
#include "network/protocol/EntityEventPacket.h"

namespace cursestone::entity {

bool Living::applyDamage(float amount) {
    bool was_alive = health_ > 0.0f;
    // Vanilla absorption (generic.absorption) eats the damage before it can
    // touch health. A hit fully absorbed leaves the victim unharmed — no
    // health drop and therefore no hurt flash.
    if (absorption_ > 0.0f && amount > 0.0f) {
        float absorbed = absorption_ < amount ? absorption_ : amount;
        absorption_ -= absorbed;
        amount -= absorbed;
        onAbsorptionChanged();
    }
    if (amount <= 0.0f)
        return false;
    health_ -= amount;
    if (health_ < 0)
        health_ = 0;
    bool just_died = was_alive && health_ <= 0.0f;
    if (just_died)
        onDeath();
    else
        onHealthChanged(); // Immediately synchronize health + hurt flash.
    return just_died;
}

void Living::setAbsorption(float v) {
    float clamped = v < 0.0f ? 0.0f : v;
    if (clamped == absorption_)
        return;
    absorption_ = clamped;
    onAbsorptionChanged();
}

// Default health-changed reaction: push the red hurt flash to every client
// in the world. EntityEvent 2 tints the entity's model red for a few frames
// on each viewer, which is the only way a 0.15.10 client can see that a
// body it does not control just took damage. Lethal hits skip this and go
// through onDeath() instead — the death animation carries its own tint.
void Living::onHealthChanged() {
    broadcastHurtFlash();
}

void Living::broadcastHurtFlash() {
    const auto& players = Server::getInstance().playersSnapshotRef();
    World* lvl = world_.get();
    // Audience pre-pass: the packet object is only allocated when at least
    // one spawned player of this world would receive it, so zero-viewer
    // damage (single-player mob noise, empty worlds) never touches the heap.
    bool audience = false;
    for (const auto& p : players) {
        if (p && p->spawned() && !p->isPendingRemoval() && p->getWorld() == lvl) {
            audience = true;
            break;
        }
    }
    if (!audience)
        return;
    // The event packet is immutable past this point, so one shared instance
    // serves the whole broadcast without per-viewer copies. Each viewer's
    // unordered stream flushes right after its copy goes out: the hurt
    // flash is perceived-latency-critical feedback, and waiting for the
    // end-of-tick batch flush puts the red flash a full phase behind the
    // strike on heavy ticks.
    auto ev = std::make_shared<network::protocol::EntityEventPacket>();
    ev->eid = runtimeId();
    ev->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
    for (auto& p : players) {
        if (p && p->spawned() && !p->isPendingRemoval() && p->getWorld() == lvl) {
            p->sendPacket(ev);
            p->flushUnorderedNow();
        }
    }
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
    // the era reference and the reference server process regeneration only while the entity is alive.
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

// Manage potion effects using one slot per effect ID, with the vanilla
// 0.15.10 replacement rule measured in MobEffectInstance::update
// (libminecraftpe.so 0x00BA5BF0): a higher amplifier takes BOTH the
// amplifier and the duration of the incoming effect, an equal amplifier
// only takes a strictly longer duration, and a weaker amplifier keeps the
// running effect untouched. Instant effects apply when they land.
void Living::addEffect(std::shared_ptr<Effect> e) {
    if (!e || e->isExpired())
        return;
    uint8_t id = (uint8_t)e->id();
    for (auto& existing : effects_) {
        if ((uint8_t)existing->id() != id)
            continue;
        if (e->amplifier() < existing->amplifier())
            return; // Weaker amplifier: vanilla keeps the running effect.
        if (e->amplifier() == existing->amplifier() &&
            e->duration() <= existing->duration()) {
            // Vanilla still fires the modify sync (particles refresh) even
            // when nothing changes numerically.
            onEffectModified(*existing);
            return;
        }
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

    // Snapshot effect OWNERSHIP (shared_ptr, not raw pointers): a callback
    // fired by applyEffectBehavior may remove effects, destroying them while
    // this loop is still iterating.
    std::vector<std::shared_ptr<Effect>> snapshot(effects_.begin(), effects_.end());

    std::vector<uint8_t> expired;
    for (auto& e : snapshot) {
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