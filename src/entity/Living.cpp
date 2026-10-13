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
    if (health_ < 0) health_ = 0;
    bool just_died = was_alive && health_ <= 0.0f;
    if (just_died) onDeath();
    else onHealthChanged();   // sincronización inmediata de la barra de corazones
    return just_died;
}

bool Living::damage(float amount) {
    if (isDead()) return false; // ya muerto — no re-disparar onDeath()
    auto ev = event::EventManager::get().fire<event::entity::EntityDamageEvent>(
        this, amount, event::entity::DamageCause::CUSTOM);
    if (ev.isCancelled()) return false;
    return applyDamage(ev.amount);
}

bool Living::damage(float amount, event::entity::DamageCause cause) {
    if (isDead()) return false;
    auto ev = event::EventManager::get().fire<event::entity::EntityDamageEvent>(
        this, amount, cause);
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
    // Un ente muerto NO se cura: sin este guard, la regeneracion natural
    // (Player::tickHungerAndDamage, seccion 4c) volvia a subir la vida de
    // 0 a 1 en el MISMO tick en que el golpe fatal disparaba onDeath().
    // Resultado: el chat decia "died" pero el jugador seguia vivo con un
    // trozo de corazon, y al tocar Respawn respawn() no hacia nada porque
    // isDead() ya era false. Genisys/Infinity solo procesan la
    // regeneracion mientras el ente esta vivo (Player::onUpdate hace
    // `if(!$this->isAlive() ...) return;`).
    if (isDead()) return;
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
