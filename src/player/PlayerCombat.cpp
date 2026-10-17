/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source server Software
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
#include "PlayerInternal.h"
#include "Server.h"
#include "world/World.h"
#include "inventory/PlayerInventory.h"
#include "item/ItemIds.h"
#include "item/Item.h"
#include "network/protocol/AddEntityPacket.h"
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/MobEquipmentPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "event/EventManager.h"
#include "event/player/PlayerInteractEvent.h"
#include <cmath>
#include <memory>

namespace cursestone {

// ============================================================
// Damage handling overrides for living entities.
// Creative immunity follows  behavior: check the victim's
// gamemode before firing damage events or sending damage-related packets.
// This prevents unexpected damage synchronization from crashing 0.15.10.
// ============================================================
entity::DamageApply Player::damage(float amount) {
    // Custom damage from server-side operations remains allowed in creative.
    return entity::Living::damage(amount);
}

entity::DamageApply Player::damage(float amount, event::entity::DamageCause cause) {
    // Creative players ignore all damage except void and custom damage.
    if (isCreative() && cause != event::entity::DamageCause::VOID &&
        cause != event::entity::DamageCause::CUSTOM) {
        return entity::DamageApply::CANCELLED;
    }
    // Fire Resistance blocks all fire-related damage.
    if (hasEffect(entity::EFF_FIRE_RESISTANCE) &&
        (cause == event::entity::DamageCause::FIRE ||
         cause == event::entity::DamageCause::FIRE_TICK ||
         cause == event::entity::DamageCause::LAVA)) {
        return entity::DamageApply::CANCELLED;
    }
    // Resistance reduces damage by 20% per level, except void and starvation.
    if (const entity::Effect* res = getEffect(entity::EFF_RESISTANCE)) {
        if (cause != event::entity::DamageCause::VOID &&
            cause != event::entity::DamageCause::STARVATION) {
            amount *= 1.0f - 0.2f * (res->amplifier() + 1);
        }
    }
    auto applied = entity::Living::damage(amount, cause);
    // Taking damage adds 0.3 exhaustion.
    if (applied != entity::DamageApply::CANCELLED)
        addExhaustion(0.3f);
    return applied;
}

entity::DamageApply Player::damageByEntity(float amount, entity::Entity* damager,
                                           event::entity::DamageCause cause) {
    // Creative players are immune to entity attacks and projectiles.
    if (isCreative())
        return entity::DamageApply::CANCELLED;
    // Resistance reduces damage by 20% per level.
    if (const entity::Effect* res = getEffect(entity::EFF_RESISTANCE))
        amount *= 1.0f - 0.2f * (res->amplifier() + 1);
    auto applied = entity::Living::damageByEntity(amount, damager, cause);
    // Taking damage adds 0.3 exhaustion.
    if (applied != entity::DamageApply::CANCELLED)
        addExhaustion(0.3f);
    return applied;
}

namespace {
constexpr double kPi = 3.14159265358979323846;
// uses a maximum bow durability of 385.
constexpr int kBowMaxDurability = 385;
} // namespace

void Player::handleUseItemAir(int16_t heldItemId) {
    if (!spawned_.load(std::memory_order_relaxed))
        return;
    if (isDead())
        return;
    if (!srv_)
        return;

    // Fire RIGHT_CLICK_AIR before item-specific handling so plugins can cancel use.
    {
        item::ItemStack held{};
        if (inv_)
            held = inv_->getHeldItem();
        auto ev = event::EventManager::get().fire<event::player::PlayerInteractEvent>(
            this, event::player::PlayerInteractEvent::RIGHT_CLICK_AIR, 0, 0, 0, 255, held);
        if (ev.isCancelled()) {
            if (inv_)
                sendSlot(inv_->heldSlot());
            return;
        }
    }

    if (heldItemId == (int16_t)item::ITEM_SPLASH_POTION) {
        throwSplashPotion();
        return;
    }

    // Eating/drinking in the air raises the same using-item pose the bow
    // uses, so other players actually see the eat/drink animation while it
    // happens. The completion EntityEvent(USE_ITEM) relay only covers the
    // final gulp; this covers the whole 1.6s hold. Repeated air-use packets
    // while the pose is up refresh the pose timer instead of restarting
    // it, so continuous eating never trips the expired-pose safety net.
    if (inv_) {
        item::ItemStack held = inv_->getHeldItem();
        item::Item* heldDef = item::Item::get(held.id);
        if (heldDef && heldDef->isFood(held.meta)) {
            if (!bow_pose_active_) {
                bow_start_action_ = (int64_t)srv_->getCurrentTick();
                broadcastBowPose(true);
            } else {
                bow_start_action_ = (int64_t)srv_->getCurrentTick();
            }
            return;
        }
    }

    if (heldItemId != (int16_t)item::ITEM_BOW)
        return; // Only bows use the air interaction path.

    // Repeated air-use packets while already drawing must not restart the
    // draw (the 0.15.10 client re-sends them during the charge).
    if (bow_pose_active_)
        return;

    // The tension pose is only broadcast when the bow can actually fire:
    // survival players without a single arrow in the inventory must not
    // show other players a drawing animation (the reference server only
    // reaches the armed state from a release that carries an arrow).
    if (!isCreative() && !hasArrowInStock())
        return;

    // Record the tick when the bow draw begins and broadcast the action
    // pose so other players see the tension animation.
    bow_start_action_ = (int64_t)srv_->getCurrentTick();
    broadcastBowPose(true);
}

// True when the inventory holds at least one arrow. Creative players do
// not consume arrows, so the check treats them as always stocked.
bool Player::hasArrowInStock() const {
    if (!inv_)
        return false;
    if (isCreative())
        return true;
    for (int i = 0; i < inv_->getSize(); ++i) {
        item::ItemStack it = inv_->getItem(i);
        if (it.id == item::ITEM_ARROW && it.count > 0)
            return true;
    }
    return false;
}

// Broadcasts the bow-use pose (Entity::DATA_FLAG_ACTION) to other players.
// Mirrors the reference server's setDataFlag(DATA_FLAGS, DATA_FLAG_ACTION)
// propagation, which turns into a SetEntityDataPacket per viewer.
void Player::broadcastBowPose(bool drawing) {
    if (!srv_)
        return;
    if (bow_pose_active_ == drawing)
        return; // Nothing changed — avoid duplicate metadata updates.
    bow_pose_active_ = drawing;
    auto pk = std::make_shared<network::protocol::SetEntityDataPacket>();
    pk->eid = runtimeId();
    pk->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_, drawing, isOnFire(), invisible_));
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

// Sends the currently held item to every other client. Remote clients only
// render another player's hand item from MobEquipmentPacket, so every held
// item change (hotbar switch, consumption, durability, commands) has to be
// re-broadcast — this mirrors the reference PlayerInventory::sendHeldItem()
// call that targets the holder's viewers.
void Player::broadcastHeldItemToViewers() {
    if (!srv_ || !inv_)
        return;
    auto held = inv_->getHeldItem();
    auto pk = std::make_shared<network::protocol::MobEquipmentPacket>();
    pk->eid = runtimeId();
    pk->itemId = (int16_t)held.id;
    pk->itemCount = held.count;
    pk->itemDamage = (int16_t)held.meta;
    pk->itemNbt = held.nbt;
    pk->hotbarSlot = (uint8_t)inv_->getHeldItemIndex();
    pk->selectedSlot = (uint8_t)inv_->getHeldItemIndex();
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

void Player::throwSplashPotion() {
    if (!srv_ || !inv_)
        return;
    if (isDead())
        return;
    item::ItemStack held = inv_->getHeldItem();
    if (held.id != item::ITEM_SPLASH_POTION || held.count <= 0)
        return;

    // Calculate the throw direction and apply the projectile speed.
    double yawRad = (double)yaw_ * kPi / 180.0;
    double pitchRad = (double)pitch_ * kPi / 180.0;
    double dx = -std::sin(yawRad) * std::cos(pitchRad);
    double dy = -std::sin(pitchRad);
    double dz = std::cos(yawRad) * std::cos(pitchRad);
    constexpr double kThrowPower = 1.1;

    int64_t eid = srv_->allocateEid();

    // Spawn the thrown potion for all players in the same world, including the thrower.
    auto add = std::make_shared<network::protocol::AddEntityPacket>();
    add->eid = eid;
    add->type = network::protocol::AddEntityPacket::TYPE_THROWN_POTION;
    add->x = (float)(pos_.x);
    add->y = (float)(pos_.y + kEyeHeight);
    add->z = (float)(pos_.z);
    add->speedX = (float)(dx * kThrowPower);
    add->speedY = (float)(dy * kThrowPower);
    add->speedZ = (float)(dz * kThrowPower);
    // Set the projectile orientation from its direction.
    double flat = std::sqrt(dx * dx + dz * dz);
    add->yaw = (float)(std::atan2(dx, dz) * 180.0 / kPi);
    add->pitch = (float)(std::atan2(dy, flat) * 180.0 / kPi);

    // Set metadata index 16 to the potion's metadata value. The
    // reference ThrownPotion::spawnTo ships DATA_POTION_ID inside the
    // AddEntityPacket itself; the 0.15.10 client reads it from that
    // spawn packet to render the bottle (a later SetEntityData alone
    // leaves the bottle invisible).
    network::protocol::SetEntityDataPacket::Entry me;
    me.index = network::protocol::SetEntityDataPacket::PROP_POTION_ID;
    me.type = network::protocol::SetEntityDataPacket::TYPE_SHORT;
    me.ival = held.meta;
    add->metadata.push_back(me);

    // Projectile shooter link (reference Projectile::DATA_SHOOTER_ID) so
    // the client can associate the thrown bottle with its thrower.
    network::protocol::SetEntityDataPacket::Entry shooter;
    shooter.index = network::protocol::SetEntityDataPacket::PROP_SHOOTER_ID;
    shooter.type = network::protocol::SetEntityDataPacket::TYPE_LONG;
    shooter.ival = runtimeId();
    add->metadata.push_back(shooter);

    // Send motion separately so the client can animate the projectile.
    auto motion = std::make_shared<network::protocol::SetEntityMotionPacket>();
    motion->entities.push_back(
        {eid, (float)(dx * kThrowPower), (float)(dy * kThrowPower), (float)(dz * kThrowPower)});
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || !other->spawned())
            continue;
        if (other->getWorld() != getWorld())
            continue;
        other->sendPacket(add);
        other->sendPacket(motion);
    }

    // Register the projectile for server-side physics.
    world::SplashPotionEntity p;
    p.eid = eid;
    p.shooter_eid = runtimeId();
    p.x = pos_.x;
    p.y = pos_.y + kEyeHeight;
    p.z = pos_.z;
    p.vx = dx * kThrowPower;
    p.vy = dy * kThrowPower;
    p.vz = dz * kThrowPower;
    p.meta = held.meta;
    p.spawn_tick = srv_->getCurrentTick();
    p.taken = false;
    if (getWorld())
        getWorld()->addSplashPotion(p);

    // Survival consumes one potion; creative preserves the stack.
    if (!isCreative()) {
        if (held.count > 1) {
            held.count -= 1;
            inv_->setItemInHand(held);
        } else {
            inv_->setItemInHand(item::ItemStack::EMPTY());
        }
        sendInventory();
    }
}

void Player::releaseBow() {
    if (!srv_)
        return;

    // Consume the draw state once and clear the action pose for other players.
    bool drawing = bow_start_action_ >= 0;
    int64_t startTick = bow_start_action_;
    bow_start_action_ = -1;

    broadcastBowPose(false);

    if (!drawing)
        return;
    if (isDead())
        return;

    auto inv = getInventory();
    if (!inv)
        return;
    auto held = inv->getHeldItem();
    if (held.id != item::ITEM_BOW)
        return; // The held item changed during the draw.

    auto* lvl = getWorld();
    if (!lvl)
        return;

    // Calculate the draw duration and resulting arrow power.
    int64_t diff = (int64_t)srv_->getCurrentTick() - startTick;
    if (diff < 0)
        diff = 0;
    double p = (double)diff / 20.0;
    double f = std::min((p * p + p * 2.0) / 3.0, 1.0) * 2.0;

    // Ignore taps that do not charge the bow long enough.
    if (f < 0.1 || diff < 5)
        return;

    // Survival requires an arrow; creative does not.
    item::ItemStack arrowStock;
    bool hasArrow = false;
    if (!isCreative()) {
        for (int i = 0; i < inv->getSize(); ++i) {
            item::ItemStack it = inv->getItem(i);
            if (it.id == item::ITEM_ARROW && it.count > 0) {
                arrowStock = it;
                hasArrow = true;
                break;
            }
        }
        if (!hasArrow) {
            inv->sendContents(this);
            return;
        }
    }

    // Calculate the arrow's launch direction from the player's yaw and pitch.
    double yawRad = (double)yaw_ * kPi / 180.0;
    double pitchRad = (double)pitch_ * kPi / 180.0;
    double dx = -std::sin(yawRad) * std::cos(pitchRad);
    double dy = -std::sin(pitchRad);
    double dz = std::cos(yawRad) * std::cos(pitchRad);

    int64_t eid = srv_->allocateEid();

    // Spawn the arrow for all players in the same world, including the shooter.
    auto add = std::make_shared<network::protocol::AddEntityPacket>();
    add->eid = eid;
    add->type = network::protocol::AddEntityPacket::TYPE_ARROW;
    add->x = (float)(pos_.x);
    add->y = (float)(pos_.y + kEyeHeight);
    add->z = (float)(pos_.z);
    add->speedX = (float)(dx * f);
    add->speedY = (float)(dy * f);
    add->speedZ = (float)(dz * f);
    // Derive the arrow's orientation from its trajectory.
    double flat = std::sqrt(dx * dx + dz * dz);
    float arrowYaw = (float)(std::atan2(dx, dz) * 180.0 / kPi);
    float arrowPitch = (float)(std::atan2(dy, flat) * 180.0 / kPi);
    add->yaw = arrowYaw;
    add->pitch = arrowPitch;

    // Projectile shooter link (reference Projectile::DATA_SHOOTER_ID) — the
    // property the 0.15.10 client expects inside the arrow spawn packet.
    network::protocol::SetEntityDataPacket::Entry shooter;
    shooter.index = network::protocol::SetEntityDataPacket::PROP_SHOOTER_ID;
    shooter.type = network::protocol::SetEntityDataPacket::TYPE_LONG;
    shooter.ival = runtimeId();
    add->metadata.push_back(shooter);

    // Send motion separately so the client can animate the arrow.
    auto motion = std::make_shared<network::protocol::SetEntityMotionPacket>();
    motion->entities.push_back({eid, (float)(dx * f), (float)(dy * f), (float)(dz * f)});
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || !other->spawned())
            continue;
        if (other->getWorld() != lvl)
            continue;
        other->sendPacket(add);
        other->sendPacket(motion);
    }

    // Broadcast the arrow launch sound.
    auto snd = std::make_shared<network::protocol::LevelEventPacket>();
    snd->evid = network::protocol::LevelEventPacket::EVENT_SOUND_SHOOT;
    snd->x = (float)pos_.x;
    snd->y = (float)(pos_.y + kEyeHeight);
    snd->z = (float)pos_.z;
    snd->data = 0;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || !other->spawned())
            continue;
        if (other->getWorld() != lvl)
            continue;
        other->sendPacket(snd);
    }

    // Register the arrow for server-side physics.
    world::ArrowEntity a;
    a.eid = eid;
    a.shooter_eid = runtimeId();
    a.x = pos_.x;
    a.y = pos_.y + kEyeHeight;
    a.z = pos_.z;
    a.vx = dx * f;
    a.vy = dy * f;
    a.vz = dz * f;
    a.yaw = arrowYaw;
    a.pitch = arrowPitch;
    a.lx = a.x;
    a.ly = a.y;
    a.lz = a.z;
    a.lyaw = arrowYaw;
    a.lpitch = arrowPitch;
    a.lmx = a.vx;
    a.lmy = a.vy;
    a.lmz = a.vz;
    a.spawn_tick = srv_->getCurrentTick();
    a.flight_ticks = 0;
    a.damage = 2.0f;         // Base arrow damage.
    a.critical = (f >= 2.0); // Fully charged arrows are critical.
    a.stuck = false;
    a.taken = false;
    lvl->addArrow(a);

    // Survival consumes one arrow and one bow durability point.
    if (!isCreative()) {
        if (hasArrow) {
            item::ItemStack one;
            one.id = arrowStock.id;
            one.meta = arrowStock.meta;
            one.count = 1;
            one.nbt = arrowStock.nbt;
            inv->removeItem({one});
        }
        held.meta += 1;
        if (held.meta >= kBowMaxDurability) {
            inv->setItemInHand(item::ItemStack::EMPTY());
        } else {
            inv->setItemInHand(held);
        }
    }
}

} // namespace cursestone