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
// MobDamagerProxy — a stand-in entity::Entity that represents a mob in
// the damage pipeline.
//
// Mob victims have no C++ Entity object (they are world::MobEntity
// records), but Player::damageByEntity and World::damageMob both take
// an entity::Entity* damager: they read its runtimeId() (to stamp the
// victim's last-attacker field), its networkId() (to name the attacker
// in death messages) and its position() (to aim the era knockback).
// The proxy carries exactly those three, refreshed from the mob record
// right before every use.
#pragma once
#include "CurseStone.h" // `using World = world::World;`
#include "entity/Entity.h"
#include <memory>

namespace cursestone::entity::ai {

class MobDamagerProxy final : public entity::Entity {
  public:
    MobDamagerProxy(int64_t mobEid, int32_t mobType,
                    std::shared_ptr<World> lvl)
        : Entity(mobEid, std::move(lvl)) {
        setNetworkId(mobType);
    }

    /// @brief Copies the attacker's state in before the damage call so
    ///        the pipeline reads the live mob position.
    void syncFrom(double x, double y, double z, float yaw) {
        setPosition(cursestone::math::Vector3{(float)x, (float)y, (float)z});
        setRotation(yaw, 0.0f);
    }
};

} // namespace cursestone::entity::ai
