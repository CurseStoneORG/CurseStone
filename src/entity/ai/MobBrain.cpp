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
#include "entity/ai/MobBrain.h"
#include "item/ItemIds.h"
#include "entity/mobs/Creeper.h"
#include "entity/EntityTypes.h"
#include "entity/ai/goals/CombatGoals.h"
#include "entity/ai/goals/DoorGoals.h"
#include "entity/ai/goals/MovementGoals.h"
#include "entity/ai/goals/PassiveGoals.h"
#include "entity/ai/goals/SunGoals.h"
#include "entity/ai/goals/TargetGoals.h"
#include "world/World.h"

namespace cursestone::entity::ai {

MobBrain::MobBrain(int32_t type, uint64_t seed)
    : type_(type), host_(), targets_(), goals_(), look_control_(&host_),
      move_control_(&host_), body_control_(&host_), jump_control_(&host_),
      sensing_(&host_),
      navigation_(&host_,
                  /*level=*/nullptr,
                  /*range=*/16.0f) {
    // Wire the back-references (the era Mob owned its controls through
    // getters; the facade holds pointers the brain fills in).
    host_.setNavigation(&navigation_);
    host_.setSensing(&sensing_);
    host_.setLookControl(&look_control_);
    host_.setMoveControl(&move_control_);
    host_.setJumpControl(&jump_control_);
    host_.setBodyControl(&body_control_);
    move_control_.setJumpControl(&jump_control_);
    mcp_random_seed(&host_.random(), (uint32_t)(seed ^ (seed >> 32)));
}

void MobBrain::newServerAiStep(World* world, world::MobEntity& record,
                               uint64_t tick) {
    // The facade binds to the record being simulated this tick. The
    // first bind seeds the movement-yaw channel from the persisted body
    // yaw.
    const bool first = !host_.level();
    host_.bind(record, world, tick);
    if (first)
        host_.seedRotation(record.yaw);
    navigation_.bind(world);

    // Era newServerAiStep: the idle counter, then the era checkDespawn
    // reset (a player within 32 blocks zeroes it — without the reset the
    // stroll goal would fall silent after 100 ticks). The era pass also
    // zeroes the jump flag before the goals re-arm it for this tick.
    host_.incNoActionTime();
    host_.resetNoActionTimeIfPlayerNear();
    host_.setJumping(false);

    sensing_.clearCache();
    targets_.tick(host_);
    // The era Skeleton::normalTick (the server-side half of the
    // isClientSide gate) writes the charging key every tick as
    // (getTarget() != null) — the client model draws the bow from it.
    // Stamp it here so the actuation sends exactly that verdict.
    if (arrow_attacker_)
        pose_sync_.charging = host_.targetEid() != 0;
    goals_.tick(host_);
    navigation_.tick();
    if (type_ == mobs::Creeper::kId)
        creeperServerStep();
    host_.setMoveTick(false);
    move_control_.tick();
    look_control_.tick();
    jump_control_.tick();
    // The era reconciled the body rotation client-side (the decompile
    // calls bodyControl->clientTick() from the client tick); NoAI copies
    // never run that path, so the server drives the same reconciler
    // here — the body yaw chases the movement yaw while walking and
    // the head while idling, and the move packets broadcast it.
    body_control_.tick();
}

void MobBrain::creeperServerStep() {
    // Era Creeper swell: the direction arms the fuse, 30 ticks
    // detonates. The actuation performs the explosion + removal when the
    // flag lands, and broadcasts the swell data keys the client model
    // scales/flash-renders from (keys 16 dir / 17 swell / 18 previous).
    host_.prevSwellTime = host_.swellTime;
    if (host_.getSwellDir() > 0) {
        if (host_.swellTime == 0) {
            // (the era played random.fuse here; the server has no mob
            // sound channel yet, so the fuse stays silent)
        }
        ++host_.swellTime;
        if (host_.swellTime > 29)
            host_.swellTime = 30;
        if (host_.swellTime >= 30)
            wants_explode_ = true;
    } else if (host_.getSwellDir() < 0 && host_.swellTime > 0) {
        // Era: the fuse unwinds while the target stays visible.
        --host_.swellTime;
    }
}

void MobBrain::setupGoals(int32_t type, uint8_t kind) {
    // ---- The era registrations (the exact addGoal sequences from the
    // MCPE 0.8.1 decompile mob constructors) ----
    switch (type) {
    case 32: // Zombie (Zombie.cpp): Float(0) BreakDoor(1) Melee(2, 1.0)
             // Stroll(6, 1.0) LookAtPlayer(7, 8) RandomLook(7);
             // targets: HurtBy(1) NearestAttackable(2).
    case 44: // ZombieVillager / Husk keep the zombie set (no era rows:
             // the zombie family template).
    case 47:
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(1, std::make_unique<BreakDoorGoal>(host_));
        goals_.addGoal(2, std::make_unique<MeleeAttackGoal>(host_, 1.0f, false));
        goals_.addGoal(6, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(7, std::make_unique<LookAtPlayerGoal>(host_, 8.0f));
        goals_.addGoal(7, std::make_unique<RandomLookAroundGoal>(host_));
        targets_.addGoal(1, std::make_unique<HurtByTargetGoal>(host_, 16.0f));
        targets_.addGoal(
            2, std::make_unique<NearestAttackableTargetGoal>(host_, 16.0f));
        has_goals_ = true;
        return;
    case 34: // Skeleton (Skeleton.cpp): Float(1) RestrictSun(2) FleeSun(3,
             // 1.0) ArrowAttack(4, 1.0, arrow, 60) Stroll(5, 1.0)
             // LookAtPlayer(6, 8) RandomLook(6).
    case 46: // Stray.
        arrow_attacker_ = true;
        goals_.addGoal(1, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(2, std::make_unique<RestrictSunGoal>(host_));
        goals_.addGoal(3, std::make_unique<FleeSunGoal>(host_, 1.0f));
        goals_.addGoal(4, std::make_unique<ArrowAttackGoal>(host_, 1.0f, 1, 60));
        goals_.addGoal(5, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(6, std::make_unique<LookAtPlayerGoal>(host_, 8.0f));
        goals_.addGoal(6, std::make_unique<RandomLookAroundGoal>(host_));
        targets_.addGoal(1, std::make_unique<HurtByTargetGoal>(host_, 16.0f));
        targets_.addGoal(
            2, std::make_unique<NearestAttackableTargetGoal>(host_, 16.0f));
        has_goals_ = true;
        return;
    case 33: // Creeper (Creeper.cpp): Float(1) Swell(2) Melee(4, 1.25)
             // Stroll(5, 1.0) LookAtPlayer(6, 8) RandomLook(6).
        goals_.addGoal(1, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(2, std::make_unique<SwellGoal>(host_));
        goals_.addGoal(4, std::make_unique<MeleeAttackGoal>(host_, 1.25f, false));
        goals_.addGoal(5, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(6, std::make_unique<LookAtPlayerGoal>(host_, 8.0f));
        goals_.addGoal(6, std::make_unique<RandomLookAroundGoal>(host_));
        targets_.addGoal(
            2, std::make_unique<NearestAttackableTargetGoal>(host_, 16.0f));
        targets_.addGoal(2, std::make_unique<HurtByTargetGoal>(host_, 16.0f));
        has_goals_ = true;
        return;
    case 12: // Pig (Pig.cpp): Float(0) Panic(1, 1.5) Tempt(3, potato/
             // carrot) Stroll(5, 1.0) LookAtPlayer(6, 6) RandomLook(7).
             // (The era list also had Breed/FollowParent: no love/age
             // system exists on this server, so those two stay out.)
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(1, std::make_unique<PanicGoal>(host_, 1.5f));
        goals_.addGoal(3, std::unique_ptr<TemptGoal>(new TemptGoal(
                              host_, 1.0f,
                              {item::ITEM_POTATO, item::ITEM_CARROT}, false)));
        goals_.addGoal(5, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(6, std::make_unique<LookAtPlayerGoal>(host_, 6.0f));
        goals_.addGoal(7, std::make_unique<RandomLookAroundGoal>(host_));
        has_goals_ = true;
        return;
    case 11: // Cow (Cow.cpp): Float(0) Panic(1, 1.5) Tempt(3, wheat)
             // Stroll(5, 0.8) LookAtPlayer(6, 6) RandomLook(7).
    case 16: // Mooshroom.
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(1, std::make_unique<PanicGoal>(host_, 1.5f));
        goals_.addGoal(3, std::unique_ptr<TemptGoal>(
                              new TemptGoal(host_, 1.0f, {item::ITEM_WHEAT}, false)));
        goals_.addGoal(5, std::make_unique<RandomStrollGoal>(host_, 0.8f));
        goals_.addGoal(6, std::make_unique<LookAtPlayerGoal>(host_, 6.0f));
        goals_.addGoal(7, std::make_unique<RandomLookAroundGoal>(host_));
        has_goals_ = true;
        return;
    case 13: // Sheep (Sheep.cpp): EatTileGoal(5, not owned in the era —
             // the port owns it) before the stroll (6).
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(1, std::make_unique<PanicGoal>(host_, 1.5f));
        goals_.addGoal(3, std::unique_ptr<TemptGoal>(
                              new TemptGoal(host_, 1.0f, {item::ITEM_WHEAT}, false)));
        goals_.addGoal(5, std::make_unique<EatTileGoal>(host_));
        goals_.addGoal(6, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(7, std::make_unique<LookAtPlayerGoal>(host_, 6.0f));
        goals_.addGoal(8, std::make_unique<RandomLookAroundGoal>(host_));
        has_goals_ = true;
        return;
    case 10: // Chicken (Chicken.cpp): Tempt(3, wheat seeds).
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(1, std::make_unique<PanicGoal>(host_, 1.5f));
        goals_.addGoal(3, std::unique_ptr<TemptGoal>(new TemptGoal(
                              host_, 1.0f, {item::ITEM_WHEAT_SEEDS}, false)));
        goals_.addGoal(5, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(6, std::make_unique<LookAtPlayerGoal>(host_, 6.0f));
        goals_.addGoal(7, std::make_unique<RandomLookAroundGoal>(host_));
        has_goals_ = true;
        return;
    case 36: // ZombiePigman: the era mob ran the old AI; the neutral
             // shape here retaliates without hunting (no
             // NearestAttackableTargetGoal).
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(2, std::make_unique<MeleeAttackGoal>(host_, 1.0f, false));
        goals_.addGoal(6, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(7, std::make_unique<LookAtPlayerGoal>(host_, 8.0f));
        goals_.addGoal(7, std::make_unique<RandomLookAroundGoal>(host_));
        targets_.addGoal(1, std::make_unique<HurtByTargetGoal>(host_, 16.0f));
        has_goals_ = true;
        return;
    default:
        break;
    }

    // ---- Family fallbacks for kinds the era source did not register
    // (documented reconstruction: the era mobs ran other movement code,
    // the closest era goal set keeps them alive on the server) ----
    if (kind == (uint8_t)mobs::MobKind::Hostile) {
        // The zombie combat template without the door goal.
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(2, std::make_unique<MeleeAttackGoal>(host_, 1.0f, false));
        goals_.addGoal(6, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(7, std::make_unique<LookAtPlayerGoal>(host_, 8.0f));
        goals_.addGoal(7, std::make_unique<RandomLookAroundGoal>(host_));
        targets_.addGoal(1, std::make_unique<HurtByTargetGoal>(host_, 16.0f));
        targets_.addGoal(
            2, std::make_unique<NearestAttackableTargetGoal>(host_, 16.0f));
        has_goals_ = true;
        return;
    }
    if (kind == (uint8_t)mobs::MobKind::Neutral) {
        // The pigman retaliate template.
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(2, std::make_unique<MeleeAttackGoal>(host_, 1.0f, false));
        goals_.addGoal(6, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(7, std::make_unique<LookAtPlayerGoal>(host_, 8.0f));
        goals_.addGoal(7, std::make_unique<RandomLookAroundGoal>(host_));
        targets_.addGoal(1, std::make_unique<HurtByTargetGoal>(host_, 16.0f));
        has_goals_ = true;
        return;
    }
    if (kind == (uint8_t)mobs::MobKind::Passive) {
        // The pig passive template without the tempt list.
        goals_.addGoal(0, std::make_unique<FloatGoal>(host_));
        goals_.addGoal(1, std::make_unique<PanicGoal>(host_, 1.5f));
        goals_.addGoal(5, std::make_unique<RandomStrollGoal>(host_, 1.0f));
        goals_.addGoal(6, std::make_unique<LookAtPlayerGoal>(host_, 6.0f));
        goals_.addGoal(7, std::make_unique<RandomLookAroundGoal>(host_));
        has_goals_ = true;
        return;
    }
    // Water / Ambient / Unknown: static, the pre-AI behavior.
}

std::unique_ptr<MobBrain> makeMobBrain(int32_t type, uint64_t seed) {
    auto brain = std::make_unique<MobBrain>(type, seed);
    brain->setupGoals(type, (uint8_t)entity::mobTypeKind(type));
    return brain;
}

} // namespace cursestone::entity::ai
