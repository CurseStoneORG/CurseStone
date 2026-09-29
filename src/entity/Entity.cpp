#include "Entity.h"
#include "level/Level.h"
namespace cursestone::entity {
Entity::Entity(int64_t runtime_id, std::shared_ptr<Level> lvl)
    : runtime_id_(runtime_id), level_(std::move(lvl)) {}
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
