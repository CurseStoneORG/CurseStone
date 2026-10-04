#include "Player.h"
#include "player/PlayerInternal.h"
#include "world/World.h"
#include "block/BlockIds.h"
#include "network/protocol/UpdateAttributesPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "event/entity/EntityDamageEvent.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace cursestone {

// --- Health sync (Infinity Player::setHealth) -------------------
// Health-only UpdateAttributesPacket: the client moves the hearts the
// instant this packet arrives. Infinity syncs the HEALTH attribute on
// EVERY setHealth() call — without this the hearts only moved on regen
// or food changes and felt stuck for seconds after taking damage.
void Player::sendHealthAttribute() {
    if (!connected_.load(std::memory_order_relaxed)) return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry h;
    h.name = "generic.health";
    h.min = 0.0f; h.max = maxHealth();
    h.value = health();
    h.defaultValue = 20.0f;
    pk->entries.push_back(h);
    sendPacket(pk);
}

// --- Attribute sync (protocol 84 UpdateAttributesPacket) ---
void Player::sendFoodLevel() {
    if (!connected_.load(std::memory_order_relaxed)) return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry e;
    // EXACT name strings from Infinity's Attribute::init (Genisys/PMMP
    // lineage): the client maps the HUD bars by these identifiers —
    // "player.hunger" for the food bar, "generic.health" for hearts.
    e.name = "player.hunger";
    e.min = 0.0f; e.max = 20.0f;
    e.value = (float)foodLevel();
    e.defaultValue = 20.0f;
    pk->entries.push_back(e);
    sendPacket(pk);
}

void Player::sendAllAttributes() {
    if (!connected_.load(std::memory_order_relaxed)) return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry h;
    h.name = "generic.health";
    h.min = 0.0f; h.max = maxHealth();
    h.value = health();
    h.defaultValue = 20.0f;
    pk->entries.push_back(h);
    network::protocol::UpdateAttributesPacket::Entry f;
    f.name = "player.hunger";
    f.min = 0.0f; f.max = 20.0f;
    f.value = (float)foodLevel();
    f.defaultValue = 20.0f;
    pk->entries.push_back(f);
    sendPacket(pk);
}

// Living hook (Living::applyDamage): runs after every non-fatal damage
// application. Mirrors two Infinity behaviours at once:
//   * Player::attack(): EntityEventPacket HURT_ANIMATION with eid=0 —
//     "me" in protocol 84 — so the client flashes red immediately.
//   * Player::setHealth(): sync the HEALTH attribute so the hearts move
//     in the same tick as the damage.
void Player::onHealthChanged() {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (isDead()) return;

    auto ee = std::make_shared<network::protocol::EntityEventPacket>();
    ee->eid = 0;   // protocol 84: eid 0 = "the local player"
    ee->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
    sendPacket(ee);

    sendHealthAttribute();
}

// --- Hunger + ambient damage pipeline (Infinity Player.php foodData/
// processMovement) — runs once per tick for every spawned player. ---
void Player::tickHungerAndDamage(uint64_t current_tick) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (isDead()) return;
    auto* lvl = srv_ ? srv_->getDefaultLevel() : nullptr;
    if (!lvl) return;

    bool creative = isCreative();
    int difficulty = srv_->difficulty();

    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int bx = (int)std::floor(pos_.x);
    int by = (int)std::floor(pos_.y);
    int bz = (int)std::floor(pos_.z);
    uint8_t at   = lvl->getBlockId(bx, by, bz);
    uint8_t body = lvl->getBlockId(bx, by + 1, bz);   // head block
    uint8_t feet = lvl->getBlockId(bx, by - 1, bz);   // stand-on block

    // ---------- 1. Drowning (Infinity Living::entityBaseTick) ----------
    // Air drains ONLY when the HEAD is submerged: Infinity checks the
    // block at y + getEyeHeight() (isInsideOfWater), NOT the block at the
    // feet. Using the feet block made the server drain air while the
    // client (which shows bubbles by camera height) still had full ones,
    // and kept drowning the player in the surface — so diving again
    // hurt as if the air was already spent.
    int eye_y = (int)std::floor(pos_.y + kEyeHeight);   // player eye height (0.15)
    uint8_t eye = lvl->getBlockId(bx, eye_y, bz);
    bool in_water = (eye == block::WATER || eye == block::FLOWING_WATER);
    if (in_water) {
        if (air_ticks_ > 0) --air_ticks_;
        else if (!creative && difficulty > 0 &&
                 now_ms - last_drown_dmg_ms_ >= 1000) {
            last_drown_dmg_ms_ = now_ms;
            damage(2.0f, event::entity::DamageCause::DROWNING); // 1 heart/s
        }
        // Water extinguishes fire (vanilla) and cancels pending fall damage.
        fire_ticks_ = 0;
        was_on_fire_ = false;
        fall_distance_ = 0.0f;
    } else {
        air_ticks_ = 300;   // 15s of air (vanilla)
    }
    // Un jugador que acaba de morir (p.ej. ahogado) no debe seguir
    // procesando el resto del tick de vida: Genisys Player::onUpdate
    // salta TODO el bloque cuando !isAlive(). Sin esto, la regeneracion
    // de la seccion 4c volvia a curar a un muerto en el mismo tick.
    if (isDead()) return;
    // Burn timer: set to 160 ticks (8s) on fire/lava contact; when it
    // reaches 0 the player stops burning (vanilla setOnFire semantics).
    if (fire_ticks_ > 0 && --fire_ticks_ == 0) was_on_fire_ = false;

    // ---------- 2. Fall damage (Infinity processMovement) ----------
    // Distance is accumulated in handleMovePlayer(); here the damage is
    // applied exactly once when the client reports ground contact with
    // >= 3 blocks of accumulated fall (vanilla: damage = fall - 3), and
    // the accumulator resets so it can start over on the next fall.
    if (!creative && difficulty > 0 && fall_distance_ >= 3.0 &&
        on_ground_fall_check_ && !in_water) {
        damage((float)(fall_distance_ - 3.0), event::entity::DamageCause::FALL);
        fall_distance_ = 0.0f;   // landing resets the fall
    }
    // Si el golpe de caida mato, terminar el tick aca: nada de lo que
    // sigue (fuego/lava/hambre/regen) debe tocar a un jugador muerto.
    if (isDead()) return;

    // ---------- 3. Fire / lava / cactus contact (Infinity checkBlockCollision) ----------
    if (at == block::LAVA || at == block::FLOWING_LAVA) {
        if (now_ms - last_lava_dmg_ms_ >= 500) {
            last_lava_dmg_ms_ = now_ms;
            if (!creative) damage(4.0f, event::entity::DamageCause::LAVA); // 2 hearts
        }
        was_in_lava_ = true;
        was_on_fire_ = true;
        setOnFire(8);
        fire_ticks_ = 160;   // 8s of burning (vanilla)
    } else {
        was_in_lava_ = false;
        if (was_on_fire_ && !creative) {
            // Fire-tick damage while burning (0.5s interval, 1 dmg vanilla).
            if (now_ms - last_fire_tick_dmg_ms_ >= 500) {
                last_fire_tick_dmg_ms_ = now_ms;
                damage(1.0f, event::entity::DamageCause::FIRE_TICK);
            }
        }
    }
    if (at == block::FIRE || body == block::FIRE) {
        if (now_ms - last_fire_dmg_ms_ >= 500) {
            last_fire_dmg_ms_ = now_ms;
            if (!creative) damage(1.0f, event::entity::DamageCause::FIRE);
        }
        was_on_fire_ = true;
        setOnFire(8);
        fire_ticks_ = 160;   // 8s of burning (vanilla)
    }
    if (at == block::CACTUS) {
        if (now_ms - last_cactus_dmg_ms_ >= 500) {
            last_cactus_dmg_ms_ = now_ms;
            if (!creative) damage(1.0f, event::entity::DamageCause::CACTUS); // half heart
        }
    }
    // Lava/fuego/cactus pueden haber matado: no seguir con hambre/regen.
    if (isDead()) return;

    // ---------- 4. Hunger (Infinity foodData) ----------
    if (!creative) {
        // 4a. Accumulate exhaustion from real actions (0.15 values).
        if (moved_this_tick_) {
            food_depletion_ += sprinting_ ? 6 : 1;   // sprint 0.1, walk 0.01 (x100)
        }
        if (in_water) food_depletion_ += 1;
        // 4b. Every 4.0 exhaustion (PMMP/Infinity threshold): drain 1
        // saturation or 1 food, then wait 80 ticks before the next drain.
        // Peaceful difficulty (0) has no food drain / starvation (vanilla).
        if (difficulty >= 1 && food_depletion_ >= 400 && food_tick_timer_ <= 0) {
            food_depletion_ -= 400;
            if (food_saturation_ > 0.0f) {
                food_saturation_ = std::max(0.0f, food_saturation_ - 1.0f);
            } else {
                int h = foodLevel();
                setFoodLevel(h > 0 ? h - 1 : 0);
            }
            food_tick_timer_ = 80;
            sendFoodLevel();
        }
        // 4c. Regen when full (17 food, every 4s) — Infinity handleOrder.
        if (foodLevel() >= 17 && health() < maxHealth() &&
            now_ms - last_regen_ms_ >= 4000) {
            last_regen_ms_ = now_ms;
            heal(1.0f);
            sendAllAttributes();
        }
        // 4d. Starvation at 0 food (1 dmg every 4s). Vanilla floors:
        // easy stops at 10 HP, normal at 1 HP, hard can kill.
        if (difficulty >= 1 && foodLevel() <= 0 && ++starve_ticks_ >= 80) {
            starve_ticks_ = 0;
            float floor_hp = (difficulty >= 3) ? 0.0f
                           : (difficulty == 2) ? 1.0f : 10.0f;
            if (health() > floor_hp) {
                damage(1.0f, event::entity::DamageCause::STARVATION);
            }
        }
    } else {
        // Creative: food is pinned at max, no drain, no starvation.
        if (foodLevel() != 20) { setFoodLevel(20); sendFoodLevel(); }
        food_saturation_ = 5.0f;
        food_depletion_ = 0;
        starve_ticks_ = 0;
    }

    (void)current_tick;
    (void)difficulty;
    (void)feet;
    (void)last_pos_y_;
    (void)last_pos_set_;
    (void)was_in_lava_;
}
void Player::setExperience(int exp, int level) {
    (void)exp; (void)level;
}
void Player::sendExperience(int exp) { (void)exp; }
void Player::sendExperienceLevel(int level) { (void)level; }
void Player::setHealth(int h) { entity::Living::setHealth((uint16_t)h); }

void Player::setMovementSpeed(float speed) { (void)speed; }
void Player::setAttribute(int attrId, float value) { (void)attrId; (void)value; }} // namespace cursestone
