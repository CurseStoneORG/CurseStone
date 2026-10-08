// EntityDamageByEntityEvent — golpe con un atacante conocido.
// Portado de pocketmine\event\entity\EntityDamageByEntityEvent (Genisys 0.15).
//
// Ademas del evento base, este es el que:
//   * guarda el knockBack (0.4 por defecto, +0.15 por nivel del
//     encantamiento Knockback del arma — lo pasa el llamador),
//   * agrega los modificadores del ATACANTE (Strength / Weakness), que en
//     Genisys se aplican al construir el evento (addAttackerModifiers).
#pragma once
#include "EntityDamageEvent.h"
#include "entity/Effect.h"
namespace cursestone::event::entity {

class EntityDamageByEntityEvent : public EntityDamageEvent {
public:
    cursestone::entity::Entity* damager = nullptr;
    float knock_back = 0.4f;

    // ($damager, $entity, $cause, $damage, $knockBack) — orden de Genisys.
    EntityDamageByEntityEvent(cursestone::entity::Entity* damager_,
                              cursestone::entity::Entity* e,
                              DamageCause cause,
                              float damage,
                              float knockBack = 0.4f)
        : EntityDamageEvent(e, cause, damage), damager(damager_), knock_back(knockBack) {
        addAttackerModifiers();
    }

    // Genisys addAttackerModifiers(): Strength sube el dano un 30% por
    // nivel (+1), Weakness lo baja un 20% por nivel (+1), y ninguno de los
    // dos puede dejar la tasa negativa.
    void addAttackerModifiers() {
        if (damager == nullptr) return;
        if (damager->hasEffectId(cursestone::entity::EFF_STRENGTH)) {
            setRateDamage(1.0f + 0.3f *
                              (float)(damager->effectAmplifier(cursestone::entity::EFF_STRENGTH) + 1),
                          DamageModifier::STRENGTH);
        }
        if (damager->hasEffectId(cursestone::entity::EFF_WEAKNESS)) {
            float rate = 1.0f - 0.2f *
                (float)(damager->effectAmplifier(cursestone::entity::EFF_WEAKNESS) + 1);
            if (rate < 0.0f) rate = 0.0f;
            setRateDamage(rate, DamageModifier::WEAKNESS);
        }
    }

    cursestone::entity::Entity* getDamager() const { return damager; }
    float getKnockBack() const { return knock_back; }
    void setKnockBack(float kb) { knock_back = kb; }

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByEntityEvent"; return n;
    }
};

} // namespace cursestone::event::entity
