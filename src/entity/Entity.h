// Entity — base class for any world entity (player, item-drop, mob,
// projectile). Holds the unique runtime id, position, motion, AABB
// and the level reference. Mirrors pocketmine/entity/Entity.php.
#pragma once
#include "main.h"   // brings in the `using Level = level::Level;` alias
#include <cstdint>
#include <memory>
#include "math/Vector3.h"
#include "math/AxisAlignedBB.h"
#include "level/Position.h"
namespace atomikpe {
namespace entity {
class Entity {
public:
    Entity(int64_t runtime_id, std::shared_ptr<Level> lvl);
    virtual ~Entity() = default;
    int64_t runtimeId() const { return runtime_id_; }
    int32_t networkId() const { return network_id_; }
    void setNetworkId(int32_t id) { network_id_ = id; }
    const atomikpe::math::Vector3& position() const { return pos_; }
    const atomikpe::math::Vector3& motion() const  { return motion_; }
    void setPosition(atomikpe::math::Vector3 p) { pos_ = p; }
    void setMotion(atomikpe::math::Vector3 m) { motion_ = m; }
    float yaw() const { return yaw_; }
    float pitch() const { return pitch_; }
    void setRotation(float yaw, float pitch) { yaw_ = yaw; pitch_ = pitch; }
    bool onGround() const { return on_ground_; }
    void setOnGround(bool b) { on_ground_ = b; }
    // Bounding box for collision (defaults to 0.6×1.8×0.6).
    virtual atomikpe::math::AxisAlignedBB getBoundingBox() const;
    // Ticks this entity. Default: gravity + friction + simple AABB
    // collision against blocks in the level.
    virtual void tick(uint64_t current_tick);
    // Returns true if this entity should be removed at next sweep.
    bool isClosed() const { return closed_; }
    void close() { closed_ = true; }
protected:
    int64_t runtime_id_;
    int32_t network_id_ = 0;
    atomikpe::math::Vector3 pos_;
    atomikpe::math::Vector3 motion_;
    float yaw_ = 0.0f, pitch_ = 0.0f;
    bool on_ground_ = false;
    bool closed_ = false;
    std::shared_ptr<Level> level_;
};
} // namespace
} // namespace
