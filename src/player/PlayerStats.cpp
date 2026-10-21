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
#include "network/protocol/MobEffectPacket.h"
#include "network/protocol/EntityEventPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "event/entity/EntityDamageEvent.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace cursestone {

// Synchronize health attributes whenever Player::setHealth() is called.
// The client updates the hearts the instant this packet arrives.
void Player::sendHealthAttribute() {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    // The client resolves attribute packets against the runtime entity
    // registry; eid 0 does not reach the local player here.
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

// Synchronize hunger, saturation, and exhaustion using protocol 84 attributes.
void Player::sendFoodLevel() {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    // Attribute packets must carry the real runtime id to reach the local player.
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

// Send all health and hunger attributes in a single packet.
void Player::sendAllAttributes() {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    // Attribute packets must carry the real runtime id to reach the local player.
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

// Apply the hurt animation and synchronize health after non-fatal damage.
// Creative players never show the hurt animation.
void Player::onHealthChanged() {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    if (isDead())
        return;

    if (!isCreative()) {
        // Self flash: the real entity id (self events resolve against it,
        // like the death animation) plus the eid 0 alias packet.
        auto ee = std::make_shared<network::protocol::EntityEventPacket>();
        ee->eid = runtimeId();
        ee->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
        sendPacket(ee);

        auto alias = std::make_shared<network::protocol::EntityEventPacket>();
        alias->eid = 0;
        alias->event = network::protocol::EntityEventPacket::HURT_ANIMATION;
        sendPacket(alias);
    }

    sendHealthAttribute();
}

// Extend the burn timer without restarting an active burn or rebroadcasting unchanged flags.
void Player::setOnFire(int seconds) {
    int ticks = seconds * 20;
    if (ticks <= fire_ticks_)
        return; // Keep the existing burn when it lasts longer.
    bool wasBurning = fire_ticks_ > 0;
    fire_ticks_ = ticks;
    if (!wasBurning)
        broadcastPoseFlags();
}

// Accumulate exhaustion and drain saturation before food, except on Peaceful difficulty.
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
    // Clamp the displayed value without discarding accumulated exhaustion.
    food_exhaustion_ = std::min(5.0f, food_exhaustion_);
    food_attrs_dirty_ = true;
}

// Create a protocol 84 effect packet for the specified entity and event.
namespace {
std::shared_ptr<network::protocol::MobEffectPacket> makeEffectPacket(uint64_t eid,
                                                                     const entity::Effect& e,
                                                                     uint8_t eventId) {
    auto pk = std::make_shared<network::protocol::MobEffectPacket>();
    pk->eid = eid;
    pk->eventId = eventId;
    pk->effectId = (uint8_t)e.id();
    pk->amplifier = (uint8_t)e.amplifier();
    pk->particles = true;
    pk->duration = e.duration();
    return pk;
}
} // namespace

// Send effect updates to the owner and other spawned players in the same world.
// MobEffectPacket is resolved against the client's entity registry, so every
// packet carries the real runtime id (eid 0 is only special-cased for
// UpdateAttributesPacket, never for effects).
void Player::sendEffectPacket(const entity::Effect& e, uint8_t eventId) {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    sendPacket(makeEffectPacket(runtimeId(), e, eventId));
    if (!srv_)
        return;
    auto others = makeEffectPacket(runtimeId(), e, eventId);
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this)
            continue;
        if (other->isPendingRemoval() || !other->spawned())
            continue;
        if (other->getWorld() != getWorld())
            continue;
        other->sendPacket(others);
    }
}

// Recompute the blended effect tint (the reference
// Entity::recalculateEffectColor: per-effect RGB weighted by
// amplifier+1) and broadcast it to same-world viewers as
// DATA_POTION_COLOR / DATA_POTION_AMBIENT metadata — the properties
// the 0.15.10 client reads to draw the swirling potion particles
// around an affected entity. An empty effect list sends the zero
// color so lingering particles clear.
// Blended potion-effect particle color; 0 when no effect is active.
uint32_t Player::blendedEffectColor() const {
    uint32_t r = 0, g = 0, b = 0;
    int count = 0;
    for (const auto& e : effects()) {
        uint32_t c = entity::Effect::colorOf(e->id());
        int weight = e->amplifier() + 1;
        r += ((c >> 16) & 0xFF) * (uint32_t)weight;
        g += ((c >> 8) & 0xFF) * (uint32_t)weight;
        b += (c & 0xFF) * (uint32_t)weight;
        count += weight;
    }
    if (count == 0)
        return 0;
    return ((r / (uint32_t)count) << 16) | ((g / (uint32_t)count) << 8) |
           (b / (uint32_t)count);
}

void Player::broadcastPotionColor() {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    uint32_t color = blendedEffectColor();
    auto pk = std::make_shared<network::protocol::SetEntityDataPacket>();
    pk->eid = runtimeId(); // Viewers resolve the tint against the real entity id.
    network::protocol::SetEntityDataPacket::Entry col;
    col.index = network::protocol::SetEntityDataPacket::PROP_POTION_COLOR;
    col.type = network::protocol::SetEntityDataPacket::TYPE_INT;
    col.ival = (int32_t)color;
    pk->entries.push_back(col);
    network::protocol::SetEntityDataPacket::Entry amb;
    amb.index = network::protocol::SetEntityDataPacket::PROP_POTION_AMBIENT;
    amb.type = network::protocol::SetEntityDataPacket::TYPE_BYTE;
    amb.ival = 0; // No ambient concept on this server yet.
    pk->entries.push_back(amb);
    // The owner needs the tint too: the client draws the swirl around the
    // local player from this metadata, not from MobEffectPacket.
    sendPacket(pk);
    if (!srv_)
        return;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this)
            continue;
        if (other->isPendingRemoval() || !other->spawned())
            continue;
        if (other->getWorld() != getWorld())
            continue;
        other->sendPacket(pk);
    }
}

// Push the underwater air property (Entity DATA_AIR, protocol 84 short) to
// the owner and every same-world viewer. The 0.15.10 client reads it to
// draw the remote bubble state; the local client also resolves eid 0.
void Player::sendAir() {
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto build = [&](int64_t eid) {
        auto pk = std::make_shared<network::protocol::SetEntityDataPacket>();
        pk->eid = eid;
        network::protocol::SetEntityDataPacket::Entry air;
        air.index = network::protocol::SetEntityDataPacket::PROP_AIR;
        air.type = network::protocol::SetEntityDataPacket::TYPE_SHORT;
        air.ival = air_ticks_;
        pk->entries.push_back(air);
        return pk;
    };
    sendPacket(build(0));
    sendPacket(build(runtimeId()));
    if (!srv_)
        return;
    auto viewerPk = build(runtimeId());
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this)
            continue;
        if (other->isPendingRemoval() || !other->spawned())
            continue;
        if (other->getWorld() != getWorld())
            continue;
        other->sendPacket(viewerPk);
    }
}

void Player::onEffectAdded(const entity::Effect& e) {
    sendEffectPacket(e, network::protocol::MobEffectPacket::EVENT_ADD);
    broadcastPotionColor();
    switch (e.id()) {
        case entity::EFF_SPEED:
        case entity::EFF_SLOWNESS:
            sendMovementSpeedAttribute();
            break;
        case entity::EFF_INVISIBILITY:
            if (!invisible_) {
                invisible_ = true;
                broadcastPoseFlags();
            }
            break;
        default:
            break;
    }
}

void Player::onEffectModified(const entity::Effect& e) {
    sendEffectPacket(e, network::protocol::MobEffectPacket::EVENT_MODIFY);
    broadcastPotionColor();
    if (e.id() == entity::EFF_SPEED || e.id() == entity::EFF_SLOWNESS)
        sendMovementSpeedAttribute();
}

void Player::onEffectRemoved(const entity::Effect& e) {
    sendEffectPacket(e, network::protocol::MobEffectPacket::EVENT_REMOVE);
    broadcastPotionColor();
    if (e.id() == entity::EFF_SPEED || e.id() == entity::EFF_SLOWNESS)
        sendMovementSpeedAttribute();
    if (e.id() == entity::EFF_INVISIBILITY && invisible_) {
        invisible_ = false;
        broadcastPoseFlags();
    }
}

// Hunger drains 0.005 exhaustion per effect level each tick.
void Player::onHungerEffectTick(int amplifier) {
    addExhaustion(0.1f * (amplifier + 1));
}

// Restore one food point and one saturation point per effect interval.
void Player::onSaturationEffectTick(int amplifier) {
    (void)amplifier;
    food_saturation_ = std::min(20.0f, food_saturation_ + 1.0f);
    setFoodLevel(std::min(20, foodLevel() + 1));
    food_attrs_dirty_ = true;
}

// Recalculate movement speed from its base to avoid stacking drift.
void Player::sendMovementSpeedAttribute() {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    float speed = movement_speed_base_;
    if (const entity::Effect* sp = getEffect(entity::EFF_SPEED))
        speed *= 1.0f + 0.2f * (sp->amplifier() + 1);
    if (const entity::Effect* sl = getEffect(entity::EFF_SLOWNESS))
        speed *= std::max(0.0f, 1.0f - 0.15f * (sl->amplifier() + 1));
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    // Attribute packets must carry the real runtime id to reach the local player.
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry e;
    e.name = "generic.movementSpeed";
    e.min = 0.0f;
    e.max = 340282346638528859811704183484516925440.0f;
    e.value = speed;
    e.defaultValue = 0.1f;
    pk->entries.push_back(e);
    sendPacket(pk);
}

// Restore active effects after login and synchronize their client-side state.
void Player::sendActiveEffects() {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    for (const auto& e : effects())
        sendPacket(makeEffectPacket(runtimeId(), *e,
                                    network::protocol::MobEffectPacket::EVENT_ADD));
    // Viewers need the tint metadata too, or the restored effects run
    // particle-less until the next add/modify/remove.
    if (!effects().empty())
        broadcastPotionColor();
    if (hasEffect(entity::EFF_INVISIBILITY))
        invisible_ = true;
    if (invisible_)
        broadcastPoseFlags();
    if (hasEffect(entity::EFF_SPEED) || hasEffect(entity::EFF_SLOWNESS))
        sendMovementSpeedAttribute();
}

// Process hunger, environmental damage, and regeneration once per spawned-player tick.
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

    // Tick potion effects and stop if they kill the player.
    tickEffects();
    if (isDead())
        return;

    uint64_t now_ms = (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::now().time_since_epoch())
                          .count();
    int bx = (int)std::floor(pos_.x);
    int by = (int)std::floor(pos_.y);
    int bz = (int)std::floor(pos_.z);
    uint8_t at = lvl->getBlockId(bx, by, bz);
    uint8_t body = lvl->getBlockId(bx, by + 1, bz); // Block at head height.
    uint8_t feet = lvl->getBlockId(bx, by - 1, bz); // Block beneath the player.

    // ---------- 1. Drowning ----------
    // Drain air only when the player's head is submerged, matching the reference server's eye-height check.
    int eye_y = (int)std::floor(pos_.y + kEyeHeight); // Player eye height.
    uint8_t eye = lvl->getBlockId(bx, eye_y, bz);
    bool in_water = (eye == block::WATER || eye == block::FLOWING_WATER);
    if (in_water) {
        // Water breathing prevents air loss.
        if (hasEffect(entity::EFF_WATER_BREATHING)) {
            if (air_ticks_ != 300) {
                air_ticks_ = 300;
                sendAir();
            }
        } else if (air_ticks_ > 0) {
            --air_ticks_;
            sendAir(); // Remote viewers track the shrinking bubble state.
        } else if (!creative && difficulty > 0 && now_ms - last_drown_dmg_ms_ >= 1000) {
            last_drown_dmg_ms_ = now_ms;
            damage(2.0f, event::entity::DamageCause::DROWNING); // One heart per second.
        }
        // Water extinguishes fire and cancels pending fall damage.
        if (fire_ticks_ > 0) {
            fire_ticks_ = 0;
            broadcastPoseFlags(); // Clear the burning flag.
        }
        was_on_fire_ = false;
        fall_distance_ = 0.0f;
    } else if (air_ticks_ != 300) {
        air_ticks_ = 300; // Restore 15 seconds of air.
        sendAir();
    }
    // Stop processing if drowning killed the player.
    if (isDead())
        return;
    // Extinguish the player when the burn timer expires.
    if (fire_ticks_ > 0 && --fire_ticks_ == 0) {
        was_on_fire_ = false;
        broadcastPoseFlags(); // Clear the burning flag.
    }

    // ---------- 2. Fall damage ----------
    // Apply damage once on landing after a fall of at least three blocks.
    if (!creative && difficulty > 0 && fall_distance_ >= 3.0 && on_ground_fall_check_ &&
        !in_water) {
        damage((float)(fall_distance_ - 3.0), event::entity::DamageCause::FALL);
        fall_distance_ = 0.0f; // Reset fall distance after landing.
    }
    // Stop processing if fall damage killed the player.
    if (isDead())
        return;

    // ---------- 3. Fire, lava, and cactus contact ----------
    if (at == block::LAVA || at == block::FLOWING_LAVA) {
        if (now_ms - last_lava_dmg_ms_ >= 500) {
            last_lava_dmg_ms_ = now_ms;
            if (!creative)
                damage(4.0f, event::entity::DamageCause::LAVA); // Two hearts.
        }
        was_in_lava_ = true;
        if (!creative) {
            was_on_fire_ = true;
            setOnFire(8); // Apply eight seconds of burning.
        }
    } else {
        was_in_lava_ = false;
        if (was_on_fire_ && !creative) {
            // Apply periodic damage while burning.
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
            setOnFire(8); // Apply eight seconds of burning.
        }
    }
    if (at == block::CACTUS) {
        if (now_ms - last_cactus_dmg_ms_ >= 500) {
            last_cactus_dmg_ms_ = now_ms;
            if (!creative)
                damage(1.0f, event::entity::DamageCause::CACTUS); // Half a heart.
        }
    }
    // Stop processing if environmental damage killed the player.
    if (isDead())
        return;

    // ---------- 4. Hunger ----------
    if (!creative) {
        // Movement exhaustion: sprinting costs 0.1 per meter; swimming costs 0.01.
        if (moved_this_tick_ && tick_move_dist_ > 0.0f) {
            if (sprinting_)
                addExhaustion(0.1f * tick_move_dist_);
            else if (in_water)
                addExhaustion(0.01f * tick_move_dist_);
        }
        // Regenerate one health point every four seconds when food is at least 18.
        if (foodLevel() >= 18 && health() < maxHealth() && now_ms - last_regen_ms_ >= 4000) {
            last_regen_ms_ = now_ms;
            heal(1.0f);
            addExhaustion(3.0f);
            sendAllAttributes();
            food_attrs_dirty_ = false;
        }
        // Starvation deals damage every four seconds, subject to difficulty limits.
        if (difficulty >= 1 && foodLevel() <= 0 && ++starve_ticks_ >= 80) {
            starve_ticks_ = 0;
            float floor_hp = (difficulty >= 3) ? 0.0f : (difficulty == 2) ? 1.0f : 10.0f;
            if (health() > floor_hp) {
                damage(1.0f, event::entity::DamageCause::STARVATION);
            }
        }
        // Sprinting requires more than six food points.
        if (foodLevel() <= 6 && sprinting_) {
            sprinting_ = false;
            broadcastPoseFlags();
        }
    } else {
        // Creative mode keeps food full and disables hunger drain and starvation.
        if (foodLevel() != 20) {
            setFoodLevel(20);
            sendFoodLevel();
            food_attrs_dirty_ = false;
        }
        food_saturation_ = 5.0f;
        food_exhaustion_ = 0.0f;
        starve_ticks_ = 0;
    }
    // Synchronize hunger attributes once per tick when they change.
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
// Sync the XP bar attributes (protocol 84: player.experience 0..1 and
// player.level). Like health and hunger these ride UpdateAttributesPacket
// with the real runtime id and only target the owning client.
void Player::sendExperience(int exp) {
    if (exp < 0)
        exp = 0;
    if (exp > 100)
        exp = 100;
    xp_progress_ = exp / 100.0f;
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry e;
    e.name = "player.experience";
    e.min = 0.0f;
    e.max = 1.0f;
    e.value = xp_progress_;
    e.defaultValue = 0.0f;
    pk->entries.push_back(e);
    sendPacket(pk);
}

void Player::sendExperienceLevel(int level) {
    if (level < 0)
        level = 0;
    xp_level_ = level;
    if (!connected_.load(std::memory_order_relaxed))
        return;
    auto pk = std::make_shared<network::protocol::UpdateAttributesPacket>();
    pk->eid = runtimeId();
    network::protocol::UpdateAttributesPacket::Entry e;
    e.name = "player.level";
    e.min = 0.0f;
    e.max = 2147483647.0f;
    e.value = (float)level;
    e.defaultValue = 0.0f;
    pk->entries.push_back(e);
    sendPacket(pk);
}

void Player::setExperience(int exp, int level) {
    sendExperience(exp);
    sendExperienceLevel(level);
}
void Player::setHealth(int h) {
    // Clamp health, synchronize client hearts, and route lethal changes through normal death handling.
    if (h < 0)
        h = 0;
    if (h > (int)maxHealth())
        h = (int)maxHealth();
    if (h <= 0) {
        if (!isDead()) {
            // Apply remaining health as damage to trigger the death transition once.
            applyDamage(health_ > 0.0f ? health_ : 0.0f);
        }
        return;
    }
    if (isDead())
        return; // Dead players can only return through respawn().
    entity::Living::setHealth((float)h);
    if (spawned_.load(std::memory_order_relaxed))
        sendHealthAttribute();
}

void Player::setMovementSpeed(float speed) {
    // Update the base speed used by speed and slowness effects.
    if (speed <= 0.0f)
        return;
    movement_speed_base_ = speed;
    sendMovementSpeedAttribute();
}

// Synchronize health after healing outside the hunger tick loop.
void Player::heal(float amount) {
    entity::Living::heal(amount);
    if (spawned_.load(std::memory_order_relaxed) && !isDead())
        sendHealthAttribute();
}
void Player::setAttribute(int attrId, float value) {
    (void)attrId;
    (void)value;
}
} // namespace cursestone