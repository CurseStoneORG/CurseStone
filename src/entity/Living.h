/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
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

// Base class for living entities with health, armor, and effects.
// Mobs, players, and humans derive from this class. AI is not included.
#pragma once
#include "Entity.h"
#include "Attribute.h"
#include "Effect.h"
#include "event/entity/EntityDamageEvent.h"
#include <vector>
#include <memory>

namespace cursestone::entity {

// Result of a damage() or damageByEntity() call.
// CANCELLED: No damage was applied because the entity was dead, immune, or the event was cancelled.
// HURT: Damage was applied, and the entity survived.
// KILLED: Damage was applied, and the entity died.
// Callers use this result to determine whether to synchronize the hit with clients.
// CANCELLED hits send no packets, health updates, or knockback.

enum class DamageApply {
    CANCELLED = 0,
    HURT = 1,
    KILLED = 2,
};

class Living : public Entity {
  public:
    using Entity::Entity;

    float health() const {
        return health_;
    }

    void setHealth(float h) {
        health_ = h;
    }

    float maxHealth() const {
        return max_health_;
    }

    void setMaxHealth(float m) {
        max_health_ = m;
    }

    // Apply generic damage without a known attacker using the CUSTOM cause.
    // Fires EntityDamageEvent before applying damage, allowing listeners to modify or cancel it.
    virtual DamageApply damage(float amount);

    // Apply damage from a known attacker and fire EntityDamageByEntityEvent.
    // Used by melee combat and projectile damage.
    virtual DamageApply
    damageByEntity(float amount, Entity* damager,
                   event::entity::DamageCause cause = event::entity::DamageCause::ENTITY_ATTACK);

    // Generic attack entry point matching Entity::attack(EntityDamageEvent).
    // Delegates to damage() when the attacker is unknown.
    void attack(float amount) override {
        damage(amount);
    }

    // Apply damage with an explicit cause and fire EntityDamageEvent.
    // Allows listeners to cancel or reduce damage based on its cause.
    // Used for fall damage, lava, fire, cactus, drowning, and starvation.
    virtual DamageApply damage(float amount, event::entity::DamageCause cause);

    // Restore health without exceeding maxHealth.
    virtual void heal(float amount);

    // Returns whether the entity's health is zero or lower.
    bool isDead() const {
        return health_ <= 0.0f;
    }

    // Add or upgrade an effect, replacing weaker effects with stronger ones.
    // Weaker effects are ignored; instant effects apply immediately.
    // Effect lifecycle hooks allow subclasses to synchronize client state.
    void addEffect(std::shared_ptr<Effect> e);

    // Remove the effect with the specified ID, if present.
    void removeEffect(uint8_t id);

    // Remove all active effects.
    void removeAllEffects();

    bool hasEffect(uint8_t id) const;

    // Return the active effect with the specified ID, or null if absent.
    const Effect* getEffect(uint8_t id) const;

    const std::vector<std::shared_ptr<Effect>>& effects() const {
        return effects_;
    }

    // Process effect durations, periodic behaviors, and expiration.
    void tickEffects();

    void tick(uint64_t current_tick) override;

  protected:
    // Called when health decreases without causing death.
    // Players override this to synchronize health and the hurt animation immediately.
    virtual void onHealthChanged() {
    }

    // Called when health first reaches zero or below.
    // Players override this to trigger PlayerDeathEvent.
    virtual void onDeath() {
    }

    // Apply damage, clamp health to zero, and call onDeath() when lethal.
    // Shared by damage() and damageByEntity() to avoid duplicated logic.
    bool applyDamage(float amount);

    // Effect lifecycle hooks for subclasses to synchronize client state.
    virtual void onEffectAdded(const Effect& e) {
        (void)e;
    }

    virtual void onEffectModified(const Effect& e) {
        (void)e;
    }

    virtual void onEffectRemoved(const Effect& e) {
        (void)e;
    }

    // Default hunger effect behavior does not consume food.
    virtual void onHungerEffectTick(int amplifier) {
        (void)amplifier;
    }

    // Default saturation effect behavior does not restore food.
    virtual void onSaturationEffectTick(int amplifier) {
        (void)amplifier;
    }

    // Apply the gameplay behavior of an effect tick.
    void applyEffectBehavior(const Effect& e);

    float health_ = 20.0f;
    float max_health_ = 20.0f;

    // Stores at most one active effect per ID.
    std::vector<std::shared_ptr<Effect>> effects_;
};

} // namespace cursestone