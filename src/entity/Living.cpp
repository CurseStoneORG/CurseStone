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
//   * <= 0                   -> kill() si estaba vivo (kill pone el 0)
//   * <= max (o bajando)     -> se aplica tal cual
//   * > max                  -> se recorta al maximo
//   * volver de la muerte    -> onRevive() (re-anuncio de RESPAWN)
//
// ROOT CAUSE FIX ("cuando el jugador se hace dano se manda la pantalla de
// muerte y no deja spawnear"): esta funcion ponia `health_ = 0.0f` ANTES de
// llamar a kill(), y kill() empieza con `if (health_ <= 0.0f) return;` para
// no re-entrar. Resultado: la salud ya valia 0 cuando kill() corria, el
// guard lo devolvia de inmediato y **onDeath() NO SE EJECUTABA NUNCA**
// (ni para jugadores ni para ningun Living). El jugador moria de verdad
// (`isDead()` true: el cliente recibia SetHealth 0 -> pantalla de muerte)
// pero se perdia todo el flujo de muerte:
//
//   * EntityEventPacket DEATH_ANIMATION (la animacion de morir),
//   * el RespawnPacket que arma el boton "Respawn" en el cliente 0.15.x
//     (sin el el boton no hace nada => "no deja spawnear"),
//   * PlayerDeathEvent => el mensaje "<jugador> died" en el chat,
//   * el drop del inventario y el reset de hambre/aire/fuego.
//
// PMMP `Entity::setHealth()` no toca la salud en la rama de muerte: llama a
// `$this->kill()`, y es `kill()` quien pone el 0 y llama a `onDeath()`.
// Ahora hacemos lo mismo (el 0 lo pone kill()) y el guard de re-entrada
// vuelve a servir para lo que fue escrito.
//
// `amount` se trunca a entero como en PMMP (`$amount = (int) $amount`) y en
// el cliente (`LocalPlayer::hurtTo(int)`, `Mob::actuallyHurt(int)`): la
// salud es entera, asi que un valor fraccionario no puede convertirse en un
// 0 enviado al cliente (que es lo que dispara la pantalla de muerte).
// ============================================================
// El guard de `was_alive` es lo que frena el bug de "revivir con 1 vida":
// una vez muerto, setHealth() NO vuelve a sentar nada. En Genisys esto se
// resuelve con setValue(max) solo en Player::respawn(); aqui, con revive().
void Living::setHealth(float amount) {
    const int wanted = (int)amount;          // PMMP: $amount = (int) $amount;
    if (wanted == (int)health_) return;      // mismo valor -> no hace nada

    bool was_alive = health_ > 0.0f;
    if (wanted <= 0) {
        if (was_alive) {
            kill();                          // kill() pone el 0 -> onDeath()
        } else {
            health_ = 0.0f;                  // ya estaba muerto: solo se fija
        }
    } else if (!was_alive) {
        // Muerto no recibe vida: el estado muerto solo se sale por
        // revive() (Player::respawn). Sin este guard, cualquier otra
        // jeta que mande vida post-muerte (regen, cinturon de
        // onHealthChanged/report-graph) hace "revivir" con 1 en el MISMO
        // tick del golpe fatal -> pantalla de muerte pegada + corazon
        // perdido + cuerpo invisible para los demas.
        return;
    } else if ((float)wanted <= maxHealth() || (float)wanted < health_) {
        health_ = (float)wanted;
    } else {
        health_ = maxHealth();
    }
    if (health_ > 0.0f && !was_alive) onRevive();
}

// ============================================================
// revive
// Unico camino de regreso del estado muerto. Pone la vida al valor
// pedido (recortado al maximo, minimo 1) y dispara onRevive() — que es
// lo que Player::respawn() necesita para re-anunciar el RESPAWN al
// cliente (protocolo 84: tamano de UpdateAttributes + set de flags).
// ============================================================
void Living::revive(float amount) {
    if (health_ > 0.0f) return;              // solo se revive a un muerto
    if (amount <= 0.0f) return;
    if (amount > maxHealth()) health_ = maxHealth();
    else health_ = amount;
    onRevive();
}

// ============================================================
// Genisys Living::kill() / Entity::kill()
// El guard de entrada (isAlive) es lo que evita la re-entrada: Player
// ::onDeath() vuelve a tocar la salud (setHealth(0)), y sin este guard el
// kill() de adentro dispararia onDeath() una segunda vez.
// ============================================================
void Living::kill() {
    if (health_ <= 0.0f) return;   // guard de re-entrada (ver setHealth)
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
