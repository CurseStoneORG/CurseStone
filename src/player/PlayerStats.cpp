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
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry h;
    h.name = "generic.health";
    h.min = 0.0f;
    h.max = maxHealth();
    h.value = health();
    h.defaultValue = 20.0f;
    pk->entries.push_back(h);
    sendPacket(pk);
}

// --- Attribute sync (protocol 84 UpdateAttributesPacket) ---
// One packet with the full hunger state: hunger bar, hidden saturation
// and exhaustion. The client maps the HUD bars by these identifier
// strings, so they must match the reference attribute names exactly.
void Player::sendFoodLevel() {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry e;
    e.name = "player.hunger";
    e.min = 0.0f;
    e.max = 20.0f;
    e.value = (float)foodLevel();
    e.defaultValue = 20.0f;
    pk->entries.push_back(e);
    network::protocol::UpdateAttributesPacket::Entry s;
    s.name = "player.saturation";
    s.min = 0.0f;
    s.max = 20.0f;
    s.value = food_saturation_;
    s.defaultValue = 5.0f;
    pk->entries.push_back(s);
    network::protocol::UpdateAttributesPacket::Entry x;
    x.name = "player.exhaustion";
    x.min = 0.0f;
    x.max = 5.0f;
    x.value = food_exhaustion_;
    x.defaultValue = 0.0f;
    pk->entries.push_back(x);
    sendPacket(pk);
}

void Player::sendAllAttributes() {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry h;
    h.name = "generic.health";
    h.min = 0.0f;
    h.max = maxHealth();
    h.value = health();
    h.defaultValue = 20.0f;
    pk->entries.push_back(h);
    network::protocol::UpdateAttributesPacket::Entry f;
    f.name = "player.hunger";
    f.min = 0.0f;
    f.max = 20.0f;
    f.value = (float)foodLevel();
    f.defaultValue = 20.0f;
    pk->entries.push_back(f);
    network::protocol::UpdateAttributesPacket::Entry s;
    s.name = "player.saturation";
    s.min = 0.0f;
    s.max = 20.0f;
    s.value = food_saturation_;
    s.defaultValue = 5.0f;
    pk->entries.push_back(s);
    network::protocol::UpdateAttributesPacket::Entry x;
    x.name = "player.exhaustion";
    x.min = 0.0f;
    x.max = 5.0f;
    x.value = food_exhaustion_;
    x.defaultValue = 0.0f;
    pk->entries.push_back(x);
    sendPacket(pk);
}

// Living hook (Living::applyDamage): runs after every non-fatal damage
// application. Mirrors two Infinity behaviours at once:
//   * Player::attack(): EntityEventPacket HURT_ANIMATION with eid=0 —
//     "me" in protocol 84 — so the client flashes red immediately.
//   * Player::setHealth(): sync the HEALTH attribute so the hearts move
//     in the same tick as the damage.
void Player::onHealthChanged() {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    if (isDead())
        return;

    auto ee = std::make_shared<network::protocol::EntityEventPacket>();
    ee->eid = 0; // protocol 84: eid 0 = "the local player"
    ee->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
    sendPacket(ee);

    sendHealthAttribute();
}

// Entity::setOnFire — burn-timer backend behind the fire/lava contact
// above. Extending a burn never restarts it and the ON_FIRE metadata flag
// travels with the pose byte exactly on the off->on edge: protocol 84
// clients render the flame effect from that flag alone, so keeping the
// broadcast on the transition avoids a per-tick metadata flood.
void Player::setOnFire(int seconds) {
    int ticks = seconds * 20;
    if (ticks <= fire_ticks_)
        return; // an equal or longer burn is running
    bool wasBurning = fire_ticks_ > 0;
    fire_ticks_ = ticks;
    if (!wasBurning)
        broadcastPoseFlags();
}

// addExhaustion — hunger system core. Actions (sprinting, mining,
// attacks, damage, natural regen) accumulate exhaustion; every 4.0
// units drain 1 saturation first, then 1 food. Peaceful difficulty
// never drains. Attributes sync on the next hunger tick.
void Player::addExhaustion(float amount) {
    if (amount <= 0.0f)
        return;
    food_exhaustion_ += amount;
    if (srv_ && srv_->difficulty() >= 1) {
        while (food_exhaustion_ >= 4.0f) {
            food_exhaustion_ -= 4.0f;
            if (food_saturation_ > 0.0f) {
                food_saturation_ = std::max(0.0f, food_saturation_ - 1.0f);
            } else {
                int h = foodLevel();
                setFoodLevel(h > 0 ? h - 1 : 0);
            }
        }
    }
    // Display clamp only (reference attribute max); the accumulator above
    // must keep the full amount so per-tick overflow is not lost.
    food_exhaustion_ = std::min(5.0f, food_exhaustion_);
    food_attrs_dirty_ = true;
}

// --- Hunger + ambient damage pipeline — runs once per tick for every
// spawned player. ---
void Player::tickHungerAndDamage(uint64_t current_tick) {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    if (isDead())
        return;
    auto* lvl = srv_ ? srv_->getDefaultWorld() : nullptr;
    if (!lvl)
        return;

    bool creative = isCreative();
    int difficulty = srv_->difficulty();

    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    int bx = (int)std::floor(pos_.x);
    int by = (int)std::floor(pos_.y);
    int bz = (int)std::floor(pos_.z);
    uint8_t at = lvl->getBlockId(bx, by, bz);
    uint8_t body = lvl->getBlockId(bx, by + 1, bz); // head block
    uint8_t feet = lvl->getBlockId(bx, by - 1, bz); // stand-on block

    // ---------- 1. Drowning (Infinity Living::entityBaseTick) ----------
    // Air drains ONLY when the HEAD is submerged: Infinity checks the
    // block at y + getEyeHeight() (isInsideOfWater), NOT the block at the
    // feet. Using the feet block made the server drain air while the
    // client (which shows bubbles by camera height) still had full ones,
    // and kept drowning the player in the surface — so diving again
    // hurt as if the air was already spent.
    int eye_y = (int)std::floor(pos_.y + kEyeHeight); // player eye height (0.15)
    uint8_t eye = lvl->getBlockId(bx, eye_y, bz);
    bool in_water = (eye == block::WATER || eye == block::FLOWING_WATER);
    if (in_water) {
        if (air_ticks_ > 0)
            --air_ticks_;
        else if (!creative && difficulty > 0 && now_ms - last_drown_dmg_ms_ >= 1000) {
            last_drown_dmg_ms_ = now_ms;
            damage(2.0f, event::entity::DamageCause::DROWNING); // 1 heart/s
        }
        // Water extinguishes fire (vanilla) and cancels pending fall damage.
        if (fire_ticks_ > 0) {
            fire_ticks_ = 0;
            broadcastPoseFlags(); // burn ended: clear the ON_FIRE flag
        }
        was_on_fire_ = false;
        fall_distance_ = 0.0f;
    } else {
        air_ticks_ = 300; // 15s of air (vanilla)
    }
    // Un jugador que acaba de morir (p.ej. ahogado) no debe seguir
    // procesando el resto del tick de vida: Genisys Player::onUpdate
    // salta TODO el bloque cuando !isAlive(). Sin esto, la regeneracion
    // de la seccion 4c volvia a curar a un muerto en el mismo tick.
    if (isDead())
        return;
    // Burn timer: set to 160 ticks (8s) on fire/lava contact; when it
    // reaches 0 the player stops burning (vanilla setOnFire semantics).
    if (fire_ticks_ > 0 && --fire_ticks_ == 0) {
        was_on_fire_ = false;
        broadcastPoseFlags(); // burn ended: clear the ON_FIRE flag
    }

    // ---------- 2. Fall damage (Infinity processMovement) ----------
    // Distance is accumulated in handleMovePlayer(); here the damage is
    // applied exactly once when the client reports ground contact with
    // >= 3 blocks of accumulated fall (vanilla: damage = fall - 3), and
    // the accumulator resets so it can start over on the next fall.
    if (!creative && difficulty > 0 && fall_distance_ >= 3.0 && on_ground_fall_check_ &&
        !in_water) {
        damage((float)(fall_distance_ - 3.0), event::entity::DamageCause::FALL);
        fall_distance_ = 0.0f; // landing resets the fall
    }
    // Si el golpe de caida mato, terminar el tick aca: nada de lo que
    // sigue (fuego/lava/hambre/regen) debe tocar a un jugador muerto.
    if (isDead())
        return;

    // ---------- 3. Fire / lava / cactus contact (Infinity checkBlockCollision) ----------
    if (at == block::LAVA || at == block::FLOWING_LAVA) {
        if (now_ms - last_lava_dmg_ms_ >= 500) {
            last_lava_dmg_ms_ = now_ms;
            if (!creative)
                damage(4.0f, event::entity::DamageCause::LAVA); // 2 hearts
        }
        was_in_lava_ = true;
        if (!creative) {
            was_on_fire_ = true;
            setOnFire(8); // 8s of burning (vanilla); raises the ON_FIRE flag
        }
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
            if (!creative)
                damage(1.0f, event::entity::DamageCause::FIRE);
        }
        if (!creative) {
            was_on_fire_ = true;
            setOnFire(8); // 8s of burning (vanilla); raises the ON_FIRE flag
        }
    }
    if (at == block::CACTUS) {
        if (now_ms - last_cactus_dmg_ms_ >= 500) {
            last_cactus_dmg_ms_ = now_ms;
            if (!creative)
                damage(1.0f, event::entity::DamageCause::CACTUS); // half heart
        }
    }
    // Lava/fuego/cactus pueden haber matado: no seguir con hambre/regen.
    if (isDead())
        return;

    // ---------- 4. Hunger (exhaustion model) ----------
    if (!creative) {
        // Movement exhaustion: sprint 0.1 per meter, swimming 0.01.
        if (moved_this_tick_ && tick_move_dist_ > 0.0f) {
            if (sprinting_)
                addExhaustion(0.1f * tick_move_dist_);
            else if (in_water)
                addExhaustion(0.01f * tick_move_dist_);
        }
        // 4b. Regen when well fed (>= 18 food): 1 HP every 4 s, costs
        // 3.0 exhaustion (vanilla).
        if (foodLevel() >= 18 && health() < maxHealth() && now_ms - last_regen_ms_ >= 4000) {
            last_regen_ms_ = now_ms;
            heal(1.0f);
            addExhaustion(3.0f);
            sendAllAttributes();
            food_attrs_dirty_ = false;
        }
        // 4c. Starvation at 0 food (1 dmg every 4s). Vanilla floors:
        // easy stops at 10 HP, normal at 1 HP, hard can kill.
        if (difficulty >= 1 && foodLevel() <= 0 && ++starve_ticks_ >= 80) {
            starve_ticks_ = 0;
            float floor_hp = (difficulty >= 3) ? 0.0f : (difficulty == 2) ? 1.0f : 10.0f;
            if (health() > floor_hp) {
                damage(1.0f, event::entity::DamageCause::STARVATION);
            }
        }
        // 4d. Sprinting needs more than 6 food (vanilla gate).
        if (foodLevel() <= 6 && sprinting_) {
            sprinting_ = false;
            broadcastPoseFlags();
        }
    } else {
        // Creative: food is pinned at max, no drain, no starvation.
        if (foodLevel() != 20) {
            setFoodLevel(20);
            sendFoodLevel();
            food_attrs_dirty_ = false;
        }
        food_saturation_ = 5.0f;
        food_exhaustion_ = 0.0f;
        starve_ticks_ = 0;
    }
    // Push the hunger/saturation/exhaustion attributes once per tick at
    // most, only when something changed (mining/attack/damage hooks set
    // the dirty flag from outside the tick loop).
    if (food_attrs_dirty_) {
        food_attrs_dirty_ = false;
        sendFoodLevel();
    }

    (void)current_tick;
    (void)difficulty;
    (void)feet;
    (void)last_pos_y_;
    (void)last_pos_set_;
    (void)was_in_lava_;
}
void Player::setExperience(int exp, int level) {
    (void)exp;
    (void)level;
}
void Player::sendExperience(int exp) {
    (void)exp;
}
void Player::sendExperienceLevel(int level) {
    (void)level;
}
void Player::setHealth(int h) {
    // PMMP Player::setHealth semantics (used by the Lua binding
    // player.setHealth too):
    //   * clamped to [0, maxHealth] — no negative/wrapped values can
    //     ever reach the wire (a negative generic.health attribute is
    //     exactly the kind of packet that hard-crashes 0.15 clients),
    //   * the hearts attribute syncs on EVERY call (Infinity
    //     Player::setHealth does the attribute sync here),
    //   * setHealth(0) on a live player kills through the regular
    //     pipeline (onDeath → PlayerDeathEvent, drops, death screen)
    //     so it behaves exactly like /kill.
    if (h < 0)
        h = 0;
    if (h > (int)maxHealth())
        h = (int)maxHealth();
    if (h <= 0) {
        if (!isDead()) {
            // Subtract everything that is left: applyDamage runs the
            // death transition once (and skips onHealthChanged).
            applyDamage(health_ > 0.0f ? health_ : 0.0f);
        }
        return;
    }
    if (isDead())
        return; // dead players only come back via respawn()
    entity::Living::setHealth((float)h);
    if (spawned_.load(std::memory_order_relaxed))
        sendHealthAttribute();
}

void Player::setMovementSpeed(float speed) {
    (void)speed;
}
void Player::setAttribute(int attrId, float value) {
    (void)attrId;
    (void)value;
}
} // namespace cursestone
