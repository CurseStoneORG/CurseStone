#include "Entity.h"
#include "world/World.h"
#include "event/EventManager.h"
#include "event/entity/EntityDamageEvent.h"
#include "entity/Effect.h"

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

// ============================================================
// Entity::attack($damage, EntityDamageEvent $source)
// Portado de pocketmine\entity\Entity::attack (Genisys 0.15):
//
//   1. Fire Resistance cancels FIRE / FIRE_TICK / LAVA antes de que el
//      evento llegue a ningun listener — el jugador con Resistencia al
//      Fuego no recibe NADA de esas tres causas, ni siquiera el evento.
//   2. El evento se despacha (PluginManager::callEvent). Un listener
//      puede cancelarlo o reescribir la cantidad.
//   3. Si sigue vivo: se registra como lastDamageCause y se aplica la
//      salud con getFinalDamage() (la suma de modificadores), que es el
//      numero que Genisys pasa a setHealth().
//
// Devuelve true solo si el evento NO fue cancelado (mismo contrato que
// Genisys, que devuelve false tanto para "cancelado" como para "muerto").
// ============================================================
bool Entity::attack(float /*damage*/, event::entity::EntityDamageEvent& source) {
    using event::entity::DamageCause;

    if (hasEffectId(EFF_FIRE_RESISTANCE)) {   // mismo namespace (cursestone::entity)
        const auto cause = source.cause();
        if (cause == DamageCause::FIRE || cause == DamageCause::FIRE_TICK ||
            cause == DamageCause::LAVA) {
            source.setCancelled();
        }
    }

    event::EventManager::get().call(source);
    if (source.isCancelled()) return false;

    rememberDamageCause((int)source.cause(), source.getDamage());
    applyDamageEvent(source);
    return true;
}

} // namespace cursestone::entity
