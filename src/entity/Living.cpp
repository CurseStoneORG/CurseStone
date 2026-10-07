#include "Living.h"
#include "Human.h"
#include "event/EventManager.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"
#include "event/entity/EntityDamageByChildEntityEvent.h"
#include "event/entity/EntityRegainHealthEvent.h"
#include "entity/damage/DamageCalculator.h"
#include <algorithm>
#include <cmath>

namespace cursestone::entity {

// ============================================================
// Genisys Entity::getMaxHealth():
//     maxHealth + (HEALTH_BOOST ? 4 * (amplifier + 1) : 0)
// ============================================================
float Living::maxHealth() const {
    float boost = 0.0f;
    if (hasEffectId(EFF_HEALTH_BOOST)) {
        boost = 4.0f * (float)(effectAmplifier(EFF_HEALTH_BOOST) + 1);
    }
    return max_health_ + boost;
}

// ============================================================
// Genisys Entity::setHealth + Living::setHealth
//   * mismo valor            -> no hace nada
//   * <= 0                   -> 0 y kill() si estaba vivo
//   * <= max (o bajando)     -> se aplica tal cual
//   * > max                  -> se recorta al maximo
//   * volver de la muerte    -> onRevive() (re-anuncio de RESPAWN)
// ============================================================
void Living::setHealth(float amount) {
    if (amount == health_) return;
    bool was_alive = health_ > 0.0f;
    if (amount <= 0.0f) {
        health_ = 0.0f;
        if (was_alive) kill();          // -> onDeath()
    } else if (amount <= maxHealth() || amount < health_) {
        health_ = amount;
    } else {
        health_ = maxHealth();
    }
    if (health_ > 0.0f && !was_alive) onRevive();
}

// ============================================================
// Genisys Living::kill() / Entity::kill()
// El guard de entrada (isAlive) es lo que evita la re-entrada: Player
// ::onDeath() vuelve a tocar la salud (setHealth(0)), y sin este guard el
// kill() de adentro dispararia onDeath() una segunda vez.
// ============================================================
void Living::kill() {
    if (health_ <= 0.0f) return;
    health_ = 0.0f;
    removeAllEffects();
    onDeath();
}

// ============================================================
// applyDamageEvent — paso de salud de Genisys Entity::attack().
// La rama de absorcion es la de `if($this instanceof Human)`: nosotros la
// resolvemos con el accessor virtual absorptionPoints(), que es 0 para
// todo lo que no sea un Human -> misma decision que el instanceof.
// ============================================================
void Living::applyDamageEvent(event::entity::EntityDamageEvent& source) {
    if (isDead()) return;
    float damage = std::round(source.getFinalDamage());
    int absorption = absorptionPoints();
    if (absorption > 0) {
        int left = absorption - (int)damage;
        setAbsorptionPoints(left <= 0 ? 0 : left);
        setHealth(health() + (float)left);
    } else {
        setHealth(health() - damage);
    }
    if (health_ > 0.0f) onHealthChanged();
}

// ============================================================
// Living::attack($damage, EntityDamageEvent $source)
//   1. Ventana de invulnerabilidad (attackTime del ultimo golpe recibido,
//      o noDamageTicks de spawn/respawn): el golpe se cancela SOLO si no
//      supera el dano del ultimo golpe registrado (getDamage() = BASE).
//   2. Entity::attack() -> fire resistance, dispatch, lastDamageCause,
//      salud.
//   3. Si quedo vivo: un atacante en llamas (que no sea jugador) prende
//      fuego a la victima, se aplica el knockback con el `knockBack` que
//      trae el evento (0.4 + 0.15 por nivel de Knockback) y arranca el
//      cooldown de 10 ticks.
// ============================================================
bool Living::attack(float damage, event::entity::EntityDamageEvent& source) {
    if (attack_time_ > 0 || noDamageTicks() > 0) {
        if (lastDamageCauseBaseDamage() >= damage) source.setCancelled();
    }

    if (!Entity::attack(damage, source)) return false;

    if (auto* by_entity = dynamic_cast<event::entity::EntityDamageByEntityEvent*>(&source)) {
        Entity* e = by_entity->damager;
        if (auto* by_child = dynamic_cast<event::entity::EntityDamageByChildEntityEvent*>(&source)) {
            if (by_child->child != nullptr) e = by_child->child;
        }
        if (e != nullptr) {
            if (e->isOnFire() && !e->isPlayerEntity()) {
                setOnFire(2 * worldDifficulty());
            }
            knockBack(e, damage,
                      pos_.x - e->position().x,
                      pos_.z - e->position().z,
                      by_entity->getKnockBack());
        }
    }

    attack_time_ = 10;   // 0.5 s entre golpes recibidos
    return true;
}

// Entity::attack(float) — call sites viejos sin evento (BlockFire, rayos).
void Living::attack(float amount) {
    event::entity::EntityDamageEvent ev(this, event::entity::DamageCause::CUSTOM, amount);
    attack(amount, ev);
}

// ---- Wrappers: construyen el evento y entran al pipeline real ----
// Devuelven true cuando el dano se aplico (evento no cancelado), que es
// el contrato de Genisys.
bool Living::damage(float amount) {
    return damage(amount, event::entity::DamageCause::CUSTOM);
}
bool Living::damage(float amount, event::entity::DamageCause cause) {
    event::entity::EntityDamageEvent ev(this, cause, amount);
    return attack(amount, ev);
}
bool Living::damageByEntity(float amount, Entity* damager) {
    event::entity::EntityDamageByEntityEvent ev(
        damager, this, event::entity::DamageCause::ENTITY_ATTACK, amount, 0.4f);
    return attack(amount, ev);
}

// ============================================================
// Living::heal($amount, EntityRegainHealthEvent $source)
// El evento se despacha (puede cancelarse o reescribir amount) y, si vive,
// se suman los HP y se resetea el cooldown de golpes (Genisys hace
// attackTime = 0 en Living::heal).
//
// Un ente muerto NO se cura: sin este guard, la regeneracion natural
// volvia a subir la vida de 0 a 1 en el MISMO tick en que el golpe fatal
// disparaba onDeath(). Resultado: el chat decia "died" pero el jugador
// seguia vivo con un trozo de corazon, y al tocar Respawn respawn() no
// hacia nada porque isDead() ya era false. Genisys solo procesa la
// regeneracion mientras el ente esta vivo.
// ============================================================
bool Living::heal(float amount, event::entity::EntityRegainHealthEvent& source) {
    if (isDead()) return false;
    event::EventManager::get().call(source);
    if (source.isCancelled()) return false;
    setHealth(health() + source.amount);
    attack_time_ = 0;
    return true;
}

void Living::heal(float amount) {
    event::entity::EntityRegainHealthEvent ev(this, amount, 3 /*CAUSE_CUSTOM*/);
    heal(amount, ev);
}

// ============================================================
// Living::knockBack($attacker, $damage, $x, $z, $base)
// (attacker/damage no se usan en el calculo — Genisys los recibe porque
// los plugins pueden engancharse ahi.)
// ============================================================
void Living::knockBack(Entity* attacker, float damage, double x, double z, double base) {
    (void)attacker; (void)damage;
    double mx = motion_.x, my = motion_.y, mz = motion_.z;
    damage::knockBackMotion(mx, my, mz, x, z, base);
    setMotion(math::Vector3{mx, my, mz});
}

// ============================================================
// Consultas de efectos que usa el pipeline de dano
// (Genisys Entity::hasEffect / getEffect).
// ============================================================
bool Living::hasEffectId(int effectId) const {
    for (const auto& e : effects_) {
        if (e && (int)e->id() == effectId && !e->isExpired()) return true;
    }
    return false;
}
int Living::effectAmplifier(int effectId) const {
    for (const auto& e : effects_) {
        if (e && (int)e->id() == effectId && !e->isExpired()) return e->amplifier();
    }
    return 0;
}

void Living::tick(uint64_t t) {
    Entity::tick(t);
    // Genisys Living::entityBaseTick(): el cooldown de golpes y la ventana
    // de invulnerabilidad bajan un tick por vuelta.
    if (attack_time_ > 0) --attack_time_;
    if (noDamageTicks() > 0) setNoDamageTicks(noDamageTicks() - 1);
    for (auto& e : effects_) e->tick();
    // poison: 1 dmg every 2s
    // regen: +1 hp every 1.25s
    // (omitted for brevity; effect classes implement onTick)
}
} // namespace cursestone::entity
