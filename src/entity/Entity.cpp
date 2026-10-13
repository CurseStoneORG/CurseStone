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
#include "Entity.h"
#include "world/World.h"
namespace cursestone::entity {
Entity::Entity(int64_t runtime_id, std::shared_ptr<World> lvl)
    : runtime_id_(runtime_id), world_(std::move(lvl)) {}
cursestone::math::AxisAlignedBB Entity::getBoundingBox() const {
    return {pos_.x - 0.3, pos_.y, pos_.z - 0.3,
            pos_.x + 0.3, pos_.y + 1.8, pos_.z + 0.3};
}
void Entity::tick(uint64_t) {
    // Apply gravity + drag (matches PocketMine's default Entity tick).
    motion_.y -= 0.08;        // gravity
    motion_.y *= 0.98;        // drag
    motion_.x *= 0.91;
    motion_.z *= 0.91;
    pos_.x += motion_.x;
    pos_.y += motion_.y;
    pos_.z += motion_.z;
}
} // namespace
