// EntityDamageEvent — se dispara cada vez que una entidad va a
// recibir dano, ANTES de aplicarlo a health(). Un listener puede
// reescribir `amount` (para armadura/resistencia, por ejemplo) o
// cancelar el evento para anular el dano por completo.
//
// `cause` es deliberadamente un subconjunto minimo de las causas de
// PMMP/Nukkit — solo lo que el pipeline de combate actual de AtomikPE
// puede producir de verdad (ver entity/CombatUtils.h). Se puede
// ampliar cuando se implementen caida, fuego programado, ahogamiento,
// etc.
#pragma once
#include "event/Event.h"
#include <string>
#include "event/Cancellable.h"
namespace atomikpe::entity { class Entity; }
namespace atomikpe::event::entity {

enum class DamageCause {
    CUSTOM = 0,
    ENTITY_ATTACK = 1,
    CONTACT = 2, // p.ej. tocar fuego (BlockFire::onEntityCollide)
};

class EntityDamageEvent : public Event, public Cancellable {
public:
    atomikpe::entity::Entity* entity;
    float amount = 0.0f;
    DamageCause cause = DamageCause::CUSTOM;
    EntityDamageEvent(atomikpe::entity::Entity* e = nullptr,
                      float amt = 0.0f,
                      DamageCause c = DamageCause::CUSTOM)
        : entity(e), amount(amt), cause(c) {}
    const std::string& eventName() const override {
        static std::string n = "EntityDamageEvent"; return n;
    }
};
} // namespace
