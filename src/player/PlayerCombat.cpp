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
// PlayerCombat — the bow (protocol 84, ported from Dr1xyDev/Infinity
// Player.php USE_ITEM_PACKET / PLAYER_ACTION_PACKET cases).
//
// The 0.15.10 bow handshake is a two-packet dance:
//
//   1. UseItemPacket with face == 0xFF (right-click in the AIR) while the
//      bow is in hand → the client starts the draw animation locally and
//      the server records the start tick + raises the ACTION pose flag so
//      OTHER clients see the drawing pose (Infinity: setDataFlag(
//      DATA_FLAGS, DATA_FLAG_ACTION, true); $this->startAction = tick).
//   2. PlayerActionPacket ACTION_RELEASE_ITEM (=5 on protocol 84) when the
//      player lets go → the server computes the charge force from the tick
//      diff, spawns the Arrow entity (type 80) with the shot motion and
//      consumes an arrow + a durability point (survival):
//
//          $diff = tick - startAction;  $p = $diff / 20;
//          $f = min((($p**2) + $p * 2) / 3, 1) * 2;      // 0..2
//          cancelled when $f < 0.1 or $diff < 5          // dry release
//
//      motion direction (Infinity arrow NBT):
//          x = -sin(yaw) * cos(pitch);  y = -sin(pitch);  z = cos(yaw) * cos(pitch)
//      (angles in radians from the DEGREE yaw/pitch fields.)
//
// The arrow itself lives in the World as a world::ArrowEntity record:
// tickArrows() integrates the same physics the client integrates
// (gravity 0.05) and corrects viewers with MoveEntityPacket — the same
// "client integrates, server verifies" model the dropped items use.
#include "Player.h"
#include "PlayerInternal.h"
#include "Server.h"
#include "world/World.h"
#include "inventory/PlayerInventory.h"
#include "item/ItemIds.h"
#include "network/protocol/AddEntityPacket.h"
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include <cmath>
#include <memory>

namespace cursestone {

namespace {
constexpr double kPi = 3.14159265358979323846;
// Infinity Tool::DURABILITY ... Bow durability (PMMP Item::BOW uses the
// bow tool durability 385 — Player.php: `$bow->getDamage() >= 385`).
constexpr int kBowMaxDurability = 385;
} // namespace

void Player::handleUseItemAir(int16_t heldItemId) {
    if (!spawned_.load(std::memory_order_relaxed)) return;
    if (isDead()) return;
    if (heldItemId != (int16_t)item::ITEM_BOW) return;   // only the bow uses air
    if (!srv_) return;

    // Infinity: setDataFlag(DATA_FLAGS, DATA_FLAG_ACTION, true);
    //           $this->startAction = $this->server->getTick();
    bow_start_action_ = (int64_t)srv_->getCurrentTick();

    // Raise the ACTION pose bit for the OTHER clients (the shooter's own
    // client runs the draw animation locally — same split as sneaking).
    // The whole flag byte goes out so the sneak/sprint bits survive.
    auto pk = std::make_shared<network::protocol::SetEntityDataPacket>();
    pk->eid = runtimeId();
    pk->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_, true));
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) continue;
        if (other->isPendingRemoval()) continue;
        if (!other->spawned()) continue;
        if (other->getWorld() != getWorld()) continue;
        other->sendPacket(pk);
    }
}

void Player::releaseBow() {
    if (!srv_) return;

    // One-shot: the draw state is consumed on the first release packet,
    // and the ACTION pose bit is lowered for the others no matter what
    // (Infinity clears the flag through the inventory resync paths).
    bool drawing = bow_start_action_ >= 0;
    int64_t startTick = bow_start_action_;
    bow_start_action_ = -1;

    auto clearPk = std::make_shared<network::protocol::SetEntityDataPacket>();
    clearPk->eid = runtimeId();
    clearPk->entries.push_back(makePoseFlagEntry(sneaking_, sprinting_, false));
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || other.get() == this) continue;
        if (other->isPendingRemoval()) continue;
        if (!other->spawned()) continue;
        if (other->getWorld() != getWorld()) continue;
        other->sendPacket(clearPk);
    }

    if (!drawing) return;
    if (isDead()) return;

    auto inv = getInventory();
    if (!inv) return;
    auto held = inv->getHeldItem();
    if (held.id != item::ITEM_BOW) return;   // swapped items mid-draw

    auto* lvl = getWorld();
    if (!lvl) return;

    // $diff = ($this->server->getTick() - $this->startAction);
    // $p = $diff / 20; $f = min((($p ** 2) + $p * 2) / 3, 1) * 2;
    int64_t diff = (int64_t)srv_->getCurrentTick() - startTick;
    if (diff < 0) diff = 0;
    double p = (double)diff / 20.0;
    double f = std::min((p * p + p * 2.0) / 3.0, 1.0) * 2.0;

    // if($f < 0.1 or $diff < 5) cancelled — a tap is a dry release: no
    // arrow, no durability, no sound.
    if (f < 0.1 || diff < 5) return;

    // An arrow is required in SURVIVAL (Infinity scans the inventory and
    // resyncs it when missing; creative always has one).
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
            inv->sendContents(this);   // Infinity: sendContents($this); break;
            return;
        }
    }

    // Shot direction (Infinity arrow NBT Motion) — yaw/pitch are degrees.
    double yawRad   = (double)yaw_ * kPi / 180.0;
    double pitchRad = (double)pitch_ * kPi / 180.0;
    double dx = -std::sin(yawRad) * std::cos(pitchRad);
    double dy = -std::sin(pitchRad);
    double dz =  std::cos(yawRad) * std::cos(pitchRad);

    int64_t eid = srv_->allocateEid();

    // AddEntityPacket type 80 with the shot motion — every spawned player
    // of the same world INCLUDING the shooter (the client does not predict
    // its own arrows; without this the shooter never sees them).
    auto add = std::make_shared<network::protocol::AddEntityPacket>();
    add->eid  = eid;
    add->type = network::protocol::AddEntityPacket::TYPE_ARROW;
    add->x = (float)(pos_.x);
    add->y = (float)(pos_.y + kEyeHeight);   // Infinity: $this->y + getEyeHeight()
    add->z = (float)(pos_.z);
    add->speedX = (float)(dx * f);
    add->speedY = (float)(dy * f);
    add->speedZ = (float)(dz * f);
    add->yaw = yaw_;
    add->pitch = pitch_;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || !other->spawned()) continue;
        if (other->getWorld() != lvl) continue;
        other->sendPacket(add);
    }

    // Launch sound (Infinity: new LaunchSound($this) → EVENT_SOUND_SHOOT).
    auto snd = std::make_shared<network::protocol::LevelEventPacket>();
    snd->evid = network::protocol::LevelEventPacket::EVENT_SOUND_SHOOT;
    snd->x = (float)pos_.x;
    snd->y = (float)(pos_.y + kEyeHeight);
    snd->z = (float)pos_.z;
    snd->data = 0;
    for (auto& other : srv_->playersSnapshot()) {
        if (!other || !other->spawned()) continue;
        if (other->getWorld() != lvl) continue;
        other->sendPacket(snd);
    }

    // Server-side physics record — tickArrows() takes it from here.
    world::ArrowEntity a;
    a.eid = eid;
    a.shooter_eid = runtimeId();
    a.x = pos_.x;
    a.y = pos_.y + kEyeHeight;
    a.z = pos_.z;
    a.vx = dx * f;
    a.vy = dy * f;
    a.vz = dz * f;
    a.yaw = yaw_;
    a.pitch = pitch_;
    a.lx = a.x; a.ly = a.y; a.lz = a.z;
    a.lyaw = yaw_; a.lpitch = pitch_;
    a.spawn_tick = srv_->getCurrentTick();
    a.flight_ticks = 0;
    a.damage = 2.0f;                 // Infinity Arrow: protected $damage = 2
    a.critical = (f >= 2.0);         // Infinity: createEntity(..., $f == 2)
    a.stuck = false;
    a.taken = false;
    lvl->addArrow(a);

    // Survival costs: 1 arrow + 1 bow durability point (Infinity).
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
