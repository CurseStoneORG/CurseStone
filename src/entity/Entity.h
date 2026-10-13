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
// Entity — base class for any world entity (player, item-drop, mob,
// projectile). Holds the unique runtime id, position, motion, AABB
// and the level reference. Mirrors pocketmine/entity/Entity.php.
#pragma once
#include "CurseStone.h"   // brings in the `using World = world::World;` alias
#include <cstdint>
#include <memory>
#include "math/Vector3.h"
#include "math/AxisAlignedBB.h"
#include "world/Position.h"
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

    // ---- Damage / status effects (stubs) ----
    // These exist so BlockFire::onEntityCollide can compile against a
    // fully-formed Entity API. The damage / fire-on-entity systems are
    // not implemented yet in CurseStone — the bodies are intentional no-ops
    // and marked with TODO so they're easy to find later.
    // mirrors PMMP Entity::attack(EntityDamageEvent)
    virtual void attack(float /*damage*/) { /* TODO: damage event pipeline */ }
    // mirrors PMMP Entity::setOnFire(int $seconds)
    virtual void setOnFire(int /*seconds*/) { /* TODO: fire tick tracking */ }
protected:
    int64_t runtime_id_;
    int32_t network_id_ = 0;
    cursestone::math::Vector3 pos_;
    cursestone::math::Vector3 motion_;
    float yaw_ = 0.0f, pitch_ = 0.0f;
    bool on_ground_ = false;
    bool closed_ = false;
    std::shared_ptr<World> world_;
};
} // namespace
} // namespace
