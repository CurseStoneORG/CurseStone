#pragma once
#include "Entity.h"
namespace atomikpe::entity {
class Projectile : public Entity {
public:
    using Entity::Entity;
    void setDamage(float d) { damage_ = d; }
    float damage() const { return damage_; }
    void setShooter(int64_t id) { shooter_id_ = id; }
    int64_t shooter() const { return shooter_id_; }
protected:
    float damage_ = 0.0f;
    int64_t shooter_id_ = 0;
};
} // namespace
