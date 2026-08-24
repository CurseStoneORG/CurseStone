// Living — base class for entities with health, armor, and potion
// effects. Mobs (Hostile, Passive), players, and humans derive
// from this. AI logic is *not* included.
#pragma once
#include "Entity.h"
#include "Attribute.h"
#include "Effect.h"
#include <vector>
#include <memory>
namespace atomikpe::entity {
class Living : public Entity {
public:
    using Entity::Entity;
    float health() const { return health_; }
    void setHealth(float h) { health_ = h; }
    float maxHealth() const { return max_health_; }
    void setMaxHealth(float m) { max_health_ = m; }
    // Damage the entity. Returns true if it died.
    virtual bool damage(float amount);
    // Heal the entity by amount (capped at maxHealth).
    virtual void heal(float amount);
    // Dead flag (used by sweep removal).
    bool isDead() const { return health_ <= 0.0f; }
    void addEffect(std::shared_ptr<Effect> e) { effects_.push_back(std::move(e)); }
    const std::vector<std::shared_ptr<Effect>>& effects() const { return effects_; }
    void tick(uint64_t current_tick) override;
protected:
    float health_   = 20.0f;
    float max_health_ = 20.0f;
    std::vector<std::shared_ptr<Effect>> effects_;
};
} // namespace
