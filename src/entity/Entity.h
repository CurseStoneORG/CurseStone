// Entity — base class for any world entity (player, item-drop, mob,
// projectile). Holds the unique runtime id, position, motion, AABB
// and the level reference. Mirrors pocketmine/entity/Entity.php.
//
// ---- Damage pipeline (ported from Genisys 0.15) ---------------------
// The three-level structure is the one Genisys uses:
//
//   Entity::attack($damage, EntityDamageEvent $source)      <- this file
//       fire-resistance cancel, dispatch the event, remember the
//       last damage cause, hand the health change to applyDamageEvent()
//   Living::attack($damage, EntityDamageEvent $source)      <- Living.cpp
//       invulnerability gate (attackTime / noDamageTicks), knockback,
//       hurt/death animation, attackTime = 10 (0.5 s)
//   Player::attack($damage, EntityDamageEvent $source)      <- player/PlayerDamage.cpp
//       creative / allowFlight cancels, exhaustion, hunger
//
// All three take the event BY REFERENCE and return Genisys' boolean
// ("the event was not cancelled"). The damage numbers themselves live in
// event::entity::EntityDamageEvent (modifiers + rate modifiers), so a
// listener can rewrite or cancel a hit before any health is touched.
#pragma once
#include "main.h"   // brings in the `using World = world::World;` alias
#include <cstdint>
#include <memory>
#include "math/Vector3.h"
#include "math/AxisAlignedBB.h"
#include "world/Position.h"

namespace cursestone::event::entity {
// Opaque declarations: Entity only stores the event by reference and keeps
// the two numbers the invulnerability gate reads from it (cause + BASE
// damage), so it does not need the full event definition.
enum class DamageCause : int;
class EntityDamageEvent;
}

namespace cursestone {
namespace entity {
class Entity {
public:
    Entity(int64_t runtime_id, std::shared_ptr<World> lvl);
    virtual ~Entity() = default;
    int64_t runtimeId() const { return runtime_id_; }
    int32_t networkId() const { return network_id_; }
    void setNetworkId(int32_t id) { network_id_ = id; }
    const cursestone::math::Vector3& position() const { return pos_; }
    const cursestone::math::Vector3& motion() const  { return motion_; }
    void setPosition(cursestone::math::Vector3 p) { pos_ = p; }
    void setMotion(cursestone::math::Vector3 m) { motion_ = m; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    void setRotation(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }
    bool onGround() const { return on_ground_; }
    void setOnGround(bool b) { on_ground_ = b; }
    // Bounding box for collision (defaults to 0.6×1.8×0.6).
    virtual cursestone::math::AxisAlignedBB getBoundingBox() const;
    // Ticks this entity. Default: gravity + friction + simple AABB
    // collision against blocks in the level.
    virtual void tick(uint64_t current_tick);
    // Returns true if this entity should be removed at next sweep.
    bool isClosed() const { return closed_; }
    void close() { closed_ = true; }

    // ---- Entity::attack($damage, EntityDamageEvent $source) ----
    // Genisys Entity::attack, with the health step extracted into the
    // applyDamageEvent() hook (Living owns the health field in CurseStone;
    // Genisys keeps it on Entity). Order is unchanged:
    //   1. Fire Resistance cancels FIRE / FIRE_TICK / LAVA on the spot.
    //   2. The event is dispatched; a listener may cancel it or rewrite the
    //      damage.
    //   3. On a live event the last damage cause is recorded and
    //      applyDamageEvent() applies the final damage.
    // Returns true when the event was NOT cancelled (Genisys returns false
    // for both "cancelled" and "not alive").
    virtual bool attack(float damage, event::entity::EntityDamageEvent& source);

    // One-argument convenience form, kept for the existing call sites
    // (BlockFire::onEntityCollide, World's lightning). Genisys has no such
    // overload there — those callers build a CAUSE_CONTACT / CAUSE_LIGHTNING
    // event. Living overrides it to route into the real pipeline.
    virtual void attack(float damage) { (void)damage; }

    // Health application step of Entity::attack. Overridden by Living
    // (health, clamping, absorption buffer, onDeath/onHealthChanged hooks).
    virtual void applyDamageEvent(event::entity::EntityDamageEvent& source) { (void)source; }

    // ---- Salud (Genisys las declara en Entity) ----
    // El estado de vida vive en Living, pero las firmas viven aca para que
    // el pipeline de dano las llame virtualmente igual que en Genisys.
    virtual void setHealth(float amount) { (void)amount; }
    virtual void heal(float amount) { (void)amount; }

    // ---- Status effects, queried by the damage pipeline ----
    // Genisys Entity::hasEffect()/getEffect(); Living answers from effects_.
    virtual bool hasEffectId(int effectId) const { (void)effectId; return false; }
    virtual int  effectAmplifier(int effectId) const { (void)effectId; return 0; }
    // Genisys Entity::isFireProof() — blazes, withers, mobs in the nether.
    virtual bool isFireProof() const { return false; }
    // Genisys `$e instanceof Player` checks inside the pipeline.
    virtual bool isPlayerEntity() const { return false; }
    // Absorption buffer (Genisys Human::getAbsorption()); 0 for everything
    // that is not a Human, which is exactly the `instanceof Human` branch.
    virtual int  absorptionPoints() const { return 0; }
    virtual void setAbsorptionPoints(int /*points*/) {}
    // Genisys Entity::getLevel()->getServer()->getDifficulty(), needed by
    // setOnFire(2 * difficulty) for a burning attacker. 1 (easy) is the
    // server default, so a plain Entity degrades to the same numbers.
    virtual int  worldDifficulty() const { return 1; }
    // Genisys Entity::isAlive() — false by default so entities without
    // health never enter the damage pipeline.
    virtual bool isAlive() const { return false; }

    // ---- Fire / invulnerability timers (Genisys Entity) ----
    bool isOnFire() const { return fire_ticks_ > 0; }
    int  fireTicks() const { return fire_ticks_; }
    void setFireTicks(int t) { fire_ticks_ = t; }
    // mirrors PMMP Entity::setOnFire(int $seconds): only ever raises the
    // remaining burn time, never shortens an existing burn.
    void setOnFire(int seconds) {
        int t = seconds * 20;
        if (t > fire_ticks_) fire_ticks_ = t;
    }
    void extinguish() { fire_ticks_ = 0; }
    int  noDamageTicks() const { return no_damage_ticks_; }
    void setNoDamageTicks(int t) { no_damage_ticks_ = t; }

    // Genisys keeps the whole EntityDamageEvent object alive in
    // lastDamageCause; the only thing the invulnerability gate reads from it
    // is getDamage() (the BASE modifier, NOT the final damage) and the cause,
    // so we keep those two values instead of making the event outlive the
    // call that created it.
    int   lastDamageCauseId() const { return last_damage_cause_; }
    float lastDamageCauseBaseDamage() const { return last_damage_base_; }
    void  rememberDamageCause(int causeId, float baseDamage) {
        last_damage_cause_ = causeId;
        last_damage_base_ = baseDamage;
    }

    // ---- Fall tracking (Genisys Entity::$fallDistance) ----
    float fallDistance() const { return fall_distance_; }
    void setFallDistance(float d) { fall_distance_ = d; }
    void resetFallDistance() { fall_distance_ = 0.0f; }

protected:
    int64_t runtime_id_;
    int32_t network_id_ = 0;
    cursestone::math::Vector3 pos_;
    cursestone::math::Vector3 motion_;
    float yaw_ = 0.0f, pitch_ = 0.0f;
    bool on_ground_ = false;
    bool closed_ = false;
    std::shared_ptr<World> world_;

    // ---- Damage state (Genisys Entity) ----
    int   fire_ticks_ = 0;          // remaining burning ticks (20 per second)
    int   no_damage_ticks_ = 0;     // spawn/respawn invulnerability window
    int   last_damage_cause_ = 14;  // DamageCause::CUSTOM — see DamageCause enum
    float last_damage_base_ = 0.0f; // BASE modifier of the last accepted hit
    float fall_distance_ = 0.0f;    // accumulated fall distance in blocks
};
} // namespace
} // namespace
