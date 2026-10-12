// EntityDamageByChildEntityEvent — dano por una entidad "hija" (la flecha
// de un esqueleto, la perla de un jugador...). Portado de
// pocketmine\event\entity\EntityDamageByChildEntityEvent (Genisys 0.15).
#pragma once
#include "EntityDamageByEntityEvent.h"
namespace cursestone::event::entity {

class EntityDamageByChildEntityEvent : public EntityDamageByEntityEvent {
public:
    cursestone::entity::Entity* child = nullptr;

    EntityDamageByChildEntityEvent(cursestone::entity::Entity* damager_,
                                   cursestone::entity::Entity* e,
                                   cursestone::entity::Entity* child_,
                                   DamageCause cause,
                                   float damage,
                                   float knockBack = 0.4f)
        : EntityDamageByEntityEvent(damager_, e, cause, damage, knockBack), child(child_) {}

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByChildEntityEvent"; return n;
    }
};

} // namespace cursestone::event::entity
