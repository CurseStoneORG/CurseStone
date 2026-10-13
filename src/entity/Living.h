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
// Living — base class for entities with health, armor, and potion
// effects. Mobs (Hostile, Passive), players, and humans derive
// from this. AI logic is *not* included.
#pragma once
#include "Entity.h"
#include "Attribute.h"
#include "Effect.h"
#include "event/entity/EntityDamageEvent.h"
#include <vector>
#include <memory>
namespace cursestone::entity {
class Living : public Entity {
public:
    using Entity::Entity;
    float health() const { return health_; }
    void setHealth(float h) { health_ = h; }
    float maxHealth() const { return max_health_; }
    void setMaxHealth(float m) { max_health_ = m; }

    // Damage the entity with a generic/no-attacker cause. Dispara
    // EntityDamageEvent (cause=CUSTOM) antes de aplicar el dano; un
    // listener puede reescribir la cantidad o cancelar el evento.
    // Devuelve true si murio como resultado de ESTA llamada (health
    // cruzo de >0 a <=0).
    virtual bool damage(float amount);

    // Igual que damage(), pero con un atacante conocido — dispara
    // EntityDamageByEntityEvent en vez de EntityDamageEvent. Usado
    // por el pipeline de combate cuerpo a cuerpo (ver
    // Player::handleInteract / INTERACT_PACKET).
    virtual bool damageByEntity(float amount, Entity* damager);

    // mirrors PMMP Entity::attack(EntityDamageEvent) — punto de
    // entrada generico sin atacante conocido (p.ej.
    // BlockFire::onEntityCollide). Delega en damage().
    void attack(float amount) override { damage(amount); }

    // Damage with an explicit cause (Infinity-style): fires
    // EntityDamageEvent with that cause so listeners can cancel/reduce
    // per-cause (armor on FALL, fire resistance on LAVA, etc.). Used by
    // the ambient damage pipeline: fall damage, lava, fire, cactus,
    // drowning, starvation.
    virtual bool damage(float amount, event::entity::DamageCause cause);

    // Heal the entity by amount (capped at maxHealth).
    virtual void heal(float amount);
    // Dead flag (used by sweep removal).
    bool isDead() const { return health_ <= 0.0f; }
    void addEffect(std::shared_ptr<Effect> e) { effects_.push_back(std::move(e)); }
    const std::vector<std::shared_ptr<Effect>>& effects() const { return effects_; }
    void tick(uint64_t current_tick) override;
protected:
    // Llamado por damage()/damageByEntity() cada vez que la salud baja
    // por dano SIN morir (cuando muere se llama onDeath()). Player lo
    // sobreescribe para sincronizar la vida al cliente AL INSTANTE
    // (barra de corazones fluida) y mandar la animacion de golpe, igual
    // que Infinity (Player::setHealth + EntityEventPacket::HURT_ANIMATION).
    virtual void onHealthChanged() {}

    // Llamado por damage()/damageByEntity() la primera vez que la
    // salud cae a <= 0. No-op por defecto (mobs genericos no
    // necesitan un evento de muerte todavia); Player lo sobreescribe
    // para disparar PlayerDeathEvent.
    virtual void onDeath() {}

    // Aplica `amount` a health_ (clamp a 0) y llama onDeath() si esta
    // fue la transicion viva->muerta. Compartido por damage() y
    // damageByEntity() para no duplicar la logica de clamping/onDeath.
    bool applyDamage(float amount);

    float health_   = 20.0f;
    float max_health_ = 20.0f;
    std::vector<std::shared_ptr<Effect>> effects_;
};
} // namespace
