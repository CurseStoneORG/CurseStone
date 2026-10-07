// EntityDamageByBlockEvent — dano causado por un bloque (fuego, cactus,
// lava, arena que cae...). Portado de
// pocketmine\event\entity\EntityDamageByBlockEvent (Genisys 0.15) pero
// guardando la POSICION del bloque en vez de un objeto Block*: asi el
// evento no depende del ciclo de vida de la instancia de bloque.
#pragma once
#include "EntityDamageEvent.h"
namespace cursestone::event::entity {

class EntityDamageByBlockEvent : public EntityDamageEvent {
public:
    int blockX = 0;
    int blockY = 0;
    int blockZ = 0;

    EntityDamageByBlockEvent(cursestone::entity::Entity* e, DamageCause cause,
                             float damage, int bx, int by, int bz)
        : EntityDamageEvent(e, cause, damage), blockX(bx), blockY(by), blockZ(bz) {}

    const std::string& eventName() const override {
        static std::string n = "EntityDamageByBlockEvent"; return n;
    }
};

} // namespace cursestone::event::entity
