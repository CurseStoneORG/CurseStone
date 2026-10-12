// Living — base class for entities with health, armor, and potion
// effects. Mobs (Hostile, Passive), players, and humans derive
// from this. AI logic is *not* included.
//
// ---- Damage pipeline (ported from Genisys 0.15) ----------------------
// Living is the middle stage: Entity::attack() fires the event and applies
// the health, Living::attack() adds the invulnerability gate, the knockback
// and the attack cooldown. See Entity.h for the full chain.
//
// The old ad-hoc `damage(amount, cause)` helpers are kept as thin wrappers
// that build the proper event and run the real pipeline, so existing call
// sites (hunger, fire, lava, drowning...) now get armor, resistance,
// enchantment protection and events for free.
#pragma once
#include "Entity.h"
#include "Attribute.h"
#include "Effect.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"
#include <vector>
#include <memory>
namespace cursestone::event::entity { class EntityRegainHealthEvent; }
namespace cursestone::entity {
class Living : public Entity {
public:
    using Entity::Entity;

    // ---- Salud ----
    float health() const { return health_; }
    // Genisys Entity::getMaxHealth(): el maximo base mas el bonus del
    // efecto HEALTH_BOOST (4 HP por nivel +1).
    float maxHealth() const;
    void  setMaxHealth(float m) { max_health_ = m; }
    // Genisys Entity::setHealth + Living::setHealth. Recorta al maximo,
    // mata si baja a 0. Un Living muerto ya NO se re-anima al recibir
    // vida: el unico camino de vuelta es revive() via respawn().
    void setHealth(float amount) override;
    // Devuelve la vida al maximo (o al valor dado) SOLO si esta muerto,
    // y dispara onRevive() (re-anuncia RESPAWN a los viewers). Es el
    // unico camino de regreso del estado muerto — lo llama
    // Player::respawn(), equivalente del setValue(max) de Genisys.
    void revive(float amount);
    // Nukkit-style integer overload used across the repo (setHealth(20)).
    void setHealth(int amount) { setHealth((float)amount); }
    bool isDead() const { return health_ <= 0.0f; }
    bool isAlive() const override { return health_ > 0.0f; }
    // Genisys Living::kill(): pone la salud en 0, limpia efectos y dispara
    // el evento de muerte (onDeath). No re-entra si ya estaba muerto.
    virtual void kill();

    // ---- Living::attack($damage, EntityDamageEvent $source) ----
    // Devuelve true si el evento NO fue cancelado (contrato de Genisys).
    bool attack(float damage, event::entity::EntityDamageEvent& source) override;
    // Igual, usando el dano BASE del evento como `damage` del gate de
    // invulnerabilidad (es lo que pasa Genisys en los golpes de bloque).
    bool attack(event::entity::EntityDamageEvent& source) {
        return attack(source.getDamage(), source);
    }
    // Entity::attack(float) sigue existiendo para los call sites viejos
    // (BlockFire, rayos): arma un evento CUSTOM y entra al pipeline real.
    void attack(float amount) override;
    // Aplicacion de la salud (paso final de Entity::attack): redondea el
    // dano final, gasta primero el escudo de absorcion y llama a los hooks
    // onDeath()/onHealthChanged().
    void applyDamageEvent(event::entity::EntityDamageEvent& source) override;

    // ---- Wrappers de compatibilidad (buildan el evento y lo disparan) ----
    bool damage(float amount);
    bool damage(float amount, event::entity::DamageCause cause);
    bool damageByEntity(float amount, Entity* damager);

    // ---- Living::heal($amount, EntityRegainHealthEvent $source) ----
    // El evento puede cancelar o reescribir la cantidad curada.
    bool heal(float amount, event::entity::EntityRegainHealthEvent& source);
    // Convenience: arma el evento con CAUSE_CUSTOM.
    void heal(float amount) override;

    // ---- Living::knockBack($attacker, $damage, $x, $z, $base = 0.4) ----
    void knockBack(Entity* attacker, float damage, double x, double z, double base = 0.4);

    // ---- Cooldown de golpes recibidos (Genisys Living::$attackTime) ----
    int  attackTime() const { return attack_time_; }
    void setAttackTime(int t) { attack_time_ = t; }

    // ---- Efectos (consultados por el pipeline de dano) ----
    bool hasEffectId(int effectId) const override;
    int  effectAmplifier(int effectId) const override;
    void addEffect(std::shared_ptr<Effect> e) { effects_.push_back(std::move(e)); }
    const std::vector<std::shared_ptr<Effect>>& effects() const { return effects_; }
    void removeAllEffects() { effects_.clear(); }
    void tick(uint64_t current_tick) override;

protected:
    // Llamado por applyDamageEvent() cada vez que la salud baja por dano
    // SIN morir. Player lo sobreescribe para sincronizar la vida al cliente
    // AL INSTANTE (barra de corazones fluida) y mandar la animacion de
    // golpe, igual que Genisys (Player::setHealth + HURT_ANIMATION).
    virtual void onHealthChanged() {}

    // Llamado una sola vez cuando la salud cruza de >0 a 0 (via kill()).
    // Player lo sobreescribe para disparar PlayerDeathEvent y soltar el
    // inventario.
    virtual void onDeath() {}

    // Llamado cuando la salud vuelve de 0 a >0 (Genisys Living::setHealth
    // re-anuncia el evento RESPAWN a los viewers).
    virtual void onRevive() {}

    float health_ = 20.0f;
    float max_health_ = 20.0f;
    int   attack_time_ = 0;   // ticks restantes de cooldown entre golpes
    std::vector<std::shared_ptr<Effect>> effects_;
};
} // namespace
