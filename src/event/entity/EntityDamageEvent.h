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
// EntityDamageEvent — se dispara cada vez que una entidad va a
// recibir dano, ANTES de aplicarlo a health(). Un listener puede
// reescribir `amount` (para armadura/resistencia, por ejemplo) o
// cancelar el evento para anular el dano por completo.
//
// `cause` es deliberadamente un subconjunto minimo de las causas de
// PMMP/Nukkit — solo lo que el pipeline de combate actual de CurseStone
// puede producir de verdad (ver entity/CombatUtils.h). Se puede
// ampliar cuando se implementen caida, fuego programado, ahogamiento,
// etc.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace cursestone::entity { class Entity; }
namespace cursestone::event::entity {

enum class DamageCause {
    CUSTOM = 0,
    ENTITY_ATTACK = 1,
    CONTACT = 2, // p.ej. tocar fuego (BlockFire::onEntityCollide)
    // Causas ambientales (protocolo 84 / Infinity EntityDamageEvent):
    FALL = 3,        // dano de caida
    LAVA = 4,        // tocar lava
    FIRE = 5,        // estar en fuego
    FIRE_TICK = 6,   // dano de arder
    DROWNING = 7,    // quedarse sin aire bajo el agua
    CACTUS = 8,      // tocar cactus
    STARVATION = 9,  // hambre en 0 (mata salvo en dificultades bajas)
    VOID = 10,       // caer al vacio
};

class EntityDamageEvent : public Event, public Cancellable {
public:
    cursestone::entity::Entity* entity;
    float amount = 0.0f;
    DamageCause cause = DamageCause::CUSTOM;
    EntityDamageEvent(cursestone::entity::Entity* e = nullptr,
                      float amt = 0.0f,
                      DamageCause c = DamageCause::CUSTOM)
        : entity(e), amount(amt), cause(c) {}
    const std::string& eventName() const override {
        static std::string n = "EntityDamageEvent"; return n;
    }
};
} // namespace
