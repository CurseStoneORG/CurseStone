/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___/

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// Mob facade implementation. Every method names the era surface it
// reproduces; the MCPE 0.8.1 reference lives in
// minecraftpe/impl/entity/Mob.cpp + the ai/ decompile folder.
#include "entity/ai/Mob.h"
#include "entity/ai/AiMath.h"
#include "CurseStone.h"
#include "Server.h"
#include "Player.h"
#include "block/BlockIds.h"
#include "entity/EntityTypes.h"
#include "entity/ai/MobDamagerProxy.h"
#include "math/Vector3.h"
#include "network/protocol/LevelEventPacket.h"
#include "network/protocol/AddEntityPacket.h"
#include "network/protocol/SetEntityDataPacket.h"
#include "network/protocol/SetEntityMotionPacket.h"
#include "world/World.h"
#include <cmath>
#include <memory>

namespace cursestone::entity::ai {
namespace {

// Melee damage the era mobs deal (the era Mob::attackDamage seeds are
// not in the 0.8.1 decompile — the field exists and
// Zombie::getAttackDamage adds the carried item's bonus, but the per
// mob values were lost). One era-normal-difficulty value for every
// melee attacker until the per-kind damage lands.
constexpr float kMobMeleeDamage = 3.0f;

// The skeleton shot parameters (ArrowAttackGoal passes them to the era
// Arrow constructor).
constexpr double kArrowVelocity = 1.6;
constexpr double kArrowInaccuracy = 12.0;

// Era setArrowHeading (b1.x EntityArrow, the same shape the server's
// Throwable path already implements): a gaussian per-axis spread of
// 0.0075 * inaccuracy on the normalized direction, scaled by velocity.
constexpr double kArrowAimSpread = 0.0075;

// Box-Muller polar rejection — the generator shape the era gaussian
// spread uses (mirrors PlayerCombat's vanillaGaussian).
double vanillaGaussian() {
    double u1, u2, s;
    do {
        u1 = (double)std::rand() / ((double)RAND_MAX + 1.0);
        u2 = (double)std::rand() / ((double)RAND_MAX + 1.0);
        s = (2.0 * u1 - 1.0) * (2.0 * u1 - 1.0) +
            (2.0 * u2 - 1.0) * (2.0 * u2 - 1.0);
    } while (s >= 1.0 || s == 0.0);
    return std::sqrt(-2.0 * std::log(s) / s) * (2.0 * u1 - 1.0);
}

} // namespace

Mob::~Mob() {
    delete proxy_;
}

void Mob::bind(world::MobEntity& record, world::World* world, uint64_t tick) {
    rec_ = &record;
    world_ = world;
    tick_ = tick;
}

int64_t Mob::eid() const {
    return rec_ ? rec_->eid : 0;
}

int32_t Mob::type() const {
    return rec_ ? rec_->type : 0;
}

double Mob::posX() const {
    return rec_ ? rec_->x : 0.0;
}

double Mob::posY() const {
    return rec_ ? rec_->y : 0.0;
}

double Mob::posZ() const {
    return rec_ ? rec_->z : 0.0;
}

float Mob::bodyYaw() const {
    return rec_ ? rec_->yaw : 0.0f;
}

void Mob::setBodyYaw(float yaw) {
    if (rec_)
        rec_->yaw = yaw;
}

float Mob::headYaw() const {
    return rec_ ? rec_->head_yaw : 0.0f;
}

void Mob::setHeadYaw(float yaw) {
    if (rec_)
        rec_->head_yaw = yaw;
}

float Mob::pitch() const {
    return rec_ ? rec_->pitch : 0.0f;
}

void Mob::setPitch(float pitch) {
    if (rec_)
        rec_->pitch = pitch;
}

float Mob::getHeadHeight() const {
    if (!rec_)
        return 1.62f;
    return entity::mobProfile(rec_->type).eye_height;
}

float Mob::entityWidth() const {
    if (!rec_)
        return 0.6f;
    return entity::mobProfile(rec_->type).width;
}

float Mob::entityHeight() const {
    if (!rec_)
        return 1.8f;
    return entity::mobProfile(rec_->type).height;
}

float Mob::getBaseSpeed() const {
    if (!rec_)
        return 0.0f;
    float base = entity::mobProfile(rec_->type).walk_speed;
    // Speed/Slowness multiply the attribute once per read, the same
    // non-accumulating shape the old actuation pass used.
    if (world_)
        base *= world_->mobSpeedMultiplier(rec_->eid);
    return base;
}

bool Mob::isInWater() const {
    if (!world_ || !rec_)
        return false;
    const int fx = math::floorToInt(rec_->x);
    const int fy = math::floorToInt(rec_->y);
    const int fz = math::floorToInt(rec_->z);
    const uint8_t feet = world_->getBlockId(fx, fy, fz);
    if (feet == block::WATER || feet == block::FLOWING_WATER)
        return true;
    // The chest cell makes the body count as submerged, matching the
    // era box-vs-material test for a standing body.
    const uint8_t chest = world_->getBlockId(fx, fy + 1, fz);
    return chest == block::WATER || chest == block::FLOWING_WATER;
}

bool Mob::isInLava() const {
    if (!world_ || !rec_)
        return false;
    const int fx = math::floorToInt(rec_->x);
    const int fy = math::floorToInt(rec_->y);
    const int fz = math::floorToInt(rec_->z);
    const uint8_t feet = world_->getBlockId(fx, fy, fz);
    if (feet == block::LAVA || feet == block::FLOWING_LAVA)
        return true;
    const uint8_t chest = world_->getBlockId(fx, fy + 1, fz);
    return chest == block::LAVA || chest == block::FLOWING_LAVA;
}

bool Mob::isAlive() const {
    return rec_ && rec_->death_tick == 0 && rec_->health > 0.0f;
}

bool Mob::isDay() const {
    // Era Level::isDay: the first half of the 24000-tick cycle.
    if (!world_)
        return false;
    const int32_t dayTick = ((world_->time() % 24000) + 24000) % 24000;
    return dayTick < 12000;
}

bool Mob::canSeeSky(int32_t x, int32_t y, int32_t z) const {
    // Era Level::canSeeSky through a bounded column scan (the server has
    // no heightmap surface the AI can query cheaply).
    if (!world_)
        return false;
    for (int32_t by = y; by <= y + 80; ++by) {
        const uint8_t id = world_->getBlockId(x, by, z);
        if (id != 0 && World::entityCollidable(id))
            return false;
    }
    return true;
}

TargetRef Mob::getTarget() const {
    TargetRef ref;
    ref.eid = target_eid_;
    ref.player = target_player_;
    if (ref.eid != 0)
        resolve(ref);
    return ref;
}

TargetRef Mob::getLastHurtByMob() const {
    TargetRef ref;
    if (!rec_ || !world_)
        return ref;
    if (rec_->last_hurt_by_eid == 0 || rec_->last_hurt_tick == 0)
        return ref;
    // The stamp expires with the era panic window (100 ticks).
    const uint64_t now = tick_;
    if (now < rec_->last_hurt_tick ||
        now - rec_->last_hurt_tick > 100)
        return ref;
    ref.eid = rec_->last_hurt_by_eid;
    ref.player = rec_->last_hurt_by_player;
    resolve(ref);
    return ref;
}

void Mob::resolve(TargetRef& ref) const {
    if (ref.eid == 0)
        return;
    if (ref.player) {
        for (auto& p : Server::getInstance().playersSnapshotRef()) {
            if (p && p->runtimeId() == ref.eid) {
                if (p->isPendingRemoval() || !p->spawned() ||
                    p->health() <= 0.0f) {
                    ref.clear();
                    return;
                }
                const auto& pos = p->getNextPosition();
                ref.x = pos.x;
                ref.y = pos.y;
                ref.z = pos.z;
                ref.width = 0.6f;
                ref.height = 1.8f;
                ref.eyeHeight = 1.62f;
                ref.alive = true;
                // The era attack-target lookup rejected creative
                // candidates (Monster::findAttackTarget, the 0x0d2c
                // creative byte); a mid-chase mode switch drops the
                // target on the next resolve.
                ref.attackable = !p->isCreative();
                return;
            }
        }
        ref.clear();
        return;
    }
    if (!world_)
        return;
    const world::MobEntity* m = world_->mob(ref.eid);
    if (!m || m->death_tick != 0 || m->health <= 0.0f) {
        ref.clear();
        return;
    }
    ref.x = m->x;
    ref.y = m->y;
    ref.z = m->z;
    const entity::MobProfile profile = entity::mobProfile(m->type);
    ref.width = profile.width;
    ref.height = profile.height;
    ref.eyeHeight = profile.eye_height;
    ref.alive = true;
}

TargetRef Mob::nearestPlayer(double range) const {
    TargetRef ref;
    if (!world_)
        return ref;
    const double range2 = range >= 0.0 ? range * range : -1.0;
    double best2 = range2 < 0.0 ? -1.0 : range2;
    for (auto& p : Server::getInstance().playersSnapshotRef()) {
        if (!p || p->isPendingRemoval() || !p->spawned())
            continue;
        if (p->getWorld() != world_)
            continue;
        if (p->health() <= 0.0f)
            continue;
        const auto& pos = p->getNextPosition();
        const double dx = pos.x - posX();
        const double dy = pos.y - posY();
        const double dz = pos.z - posZ();
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (best2 < 0.0 || d2 < best2) {
            best2 = d2;
            ref.eid = p->runtimeId();
            ref.player = true;
            ref.x = pos.x;
            ref.y = pos.y;
            ref.z = pos.z;
            ref.width = 0.6f;
            ref.height = 1.8f;
            ref.eyeHeight = 1.62f;
            ref.alive = true;
            // Same era contract as resolve(): creative bodies are look
            // candidates but never attack candidates.
            ref.attackable = !p->isCreative();
        }
    }
    return ref;
}

bool Mob::doHurtTarget(const TargetRef& victim) {
    if (!victim || !victim.alive || !world_ || !rec_)
        return false;
    if (!proxy_)
        proxy_ = new MobDamagerProxy(
            rec_->eid, rec_->type,
            Server::getInstance().getWorldShared(world_->name()));
    proxy_->syncFrom(rec_->x, rec_->y, rec_->z, rec_->yaw);
    if (victim.player) {
        for (auto& p : Server::getInstance().playersSnapshotRef()) {
            if (p && p->runtimeId() == victim.eid) {
                const entity::DamageApply applied = p->damageByEntity(
                    kMobMeleeDamage, proxy_);
                if (applied == entity::DamageApply::CANCELLED)
                    return false;
                // The era hurt() chain applies the knockback to the
                // victim right after the damage lands (Mob::hurt ->
                // knockback, 0.8.1 Mob.cpp): the shove is the hit
                // feedback the client body integrates, next to the red
                // flash the damage pipeline already broadcast.
                p->applyKnockbackFrom(rec_->x, rec_->z);
                return true;
            }
        }
        return false;
    }
    const entity::DamageApply applied = world_->damageMob(
        victim.eid, kMobMeleeDamage, proxy_);
    return applied != entity::DamageApply::CANCELLED;
}

void Mob::fireArrowAt(const TargetRef& target) {
    if (!target || !target.alive || !world_ || !rec_)
        return;

    // Era Arrow(level, holder, target, 1.6, 12.0): the aim point is the
    // victim's eye line pulled 0.1 down, the launch point is the
    // shooter's eye height.
    const double sx = rec_->x;
    const double sy = rec_->y + getHeadHeight();
    const double sz = rec_->z;
    double dx = target.x - sx;
    double dy = (target.y + target.eyeHeight - 0.1) - sy;
    double dz = target.z - sz;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-6)
        return;
    dx /= dist;
    dy /= dist;
    dz /= dist;
    dx += vanillaGaussian() * kArrowAimSpread * kArrowInaccuracy;
    dy += vanillaGaussian() * kArrowAimSpread * kArrowInaccuracy;
    dz += vanillaGaussian() * kArrowAimSpread * kArrowInaccuracy;
    const double vx = dx * kArrowVelocity;
    const double vy = dy * kArrowVelocity;
    const double vz = dz * kArrowVelocity;

    Server& srv = Server::getInstance();
    const int64_t arrowEid = srv.allocateEid();

    // Broadcast the arrow entity with its launch speed, the shooter
    // metadata link, and the bow sound (the same packet shape the
    // player's bow path sends).
    auto add = std::make_shared<network::protocol::AddEntityPacket>();
    add->eid = arrowEid;
    add->type = network::protocol::AddEntityPacket::TYPE_ARROW;
    add->x = (float)sx;
    add->y = (float)sy;
    add->z = (float)sz;
    add->speedX = (float)vx;
    add->speedY = (float)vy;
    add->speedZ = (float)vz;
    const double flat = std::sqrt(dx * dx + dz * dz);
    const float arrowYaw =
        (float)(std::atan2(dx, dz) * 180.0 / 3.14159265358979323846);
    const float arrowPitch =
        (float)(std::atan2(dy, flat) * 180.0 / 3.14159265358979323846);
    add->yaw = arrowYaw;
    add->pitch = arrowPitch;
    network::protocol::SetEntityDataPacket::Entry shooter;
    shooter.index = network::protocol::SetEntityDataPacket::PROP_SHOOTER_ID;
    shooter.type = network::protocol::SetEntityDataPacket::TYPE_LONG;
    shooter.ival = rec_->eid;
    add->metadata.push_back(shooter);

    auto snd = std::make_shared<network::protocol::LevelEventPacket>();
    snd->evid = network::protocol::LevelEventPacket::EVENT_SOUND_SHOOT;
    snd->x = (float)sx;
    snd->y = (float)sy;
    snd->z = (float)sz;
    snd->data = 0;

    world::ArrowEntity a;
    a.eid = arrowEid;
    a.shooter_eid = rec_->eid;
    a.x = sx;
    a.y = sy;
    a.z = sz;
    a.vx = vx;
    a.vy = vy;
    a.vz = vz;
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
    a.spawn_tick = srv.getCurrentTick();
    a.flight_ticks = 0;
    a.damage = 2.0f; // The era reference Arrow damage.
    a.critical = false;
    a.stuck = false;
    a.taken = false;
    world_->addArrow(a);

    for (auto& p : srv.playersSnapshotRef()) {
        if (!p || p->isPendingRemoval() || !p->spawned())
            continue;
        if (p->getWorld() != world_)
            continue;
        p->sendPacket(add);
        p->sendPacket(snd);
    }
}

} // namespace cursestone::entity::ai
