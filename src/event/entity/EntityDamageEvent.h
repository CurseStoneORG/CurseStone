// EntityDamageEvent — se dispara cada vez que una entidad va a
// recibir dano, ANTES de aplicarlo a la salud. Un listener puede
// reescribir la cantidad, cancelar el evento, o cambiar el modificador
// que quiera.
//
// Portado de pocketmine\event\entity\EntityDamageEvent (Genisys 0.15).
// La mecanica de modificadores es la de Genisys y no la de PMMP 4:
//
//   * MODIFIER_BASE       dano base crudo (el unico obligatorio).
//   * rateModifiers       multiplicadores ACUMULATIVOS (se multiplican
//                         entre si sobre el base). Genisys usa este tipo
//                         para RESISTANCE, ARMOR y PROTECTION, porque
//                         "1 - 0.04 * points" es una reduccion relativa.
//   * modifiers (>=BASE)  sumandos ADITIVOS que se agregan despues de
//                         aplicar todos los rate modifiers.
//
//   getFinalDamage() = base * (producto de rate modifiers) + suma de los
//                      modificadores aditivos distintos de BASE
//
// Igual que Genisys, la Resistencia al Dano (DAMAGE_RESISTANCE) se
// calcula en el CONSTRUCTOR: cualquier efecto de resistencia ya activo
// reduce el golpe sin que nadie tenga que acordarse de aplicarlo.
#pragma once
#include "event/Event.h"
#include "event/Cancellable.h"
#include "entity/Entity.h"
#include "entity/Effect.h"
#include <array>
#include <map>
#include <string>

namespace cursestone::event::entity {

// Genisys EntityDamageEvent::CAUSE_* — misma numeracion (linaje protocolo
// 84 / PMMP 1.6). El dano de cactus de Genisys llega como CAUSE_CONTACT
// (BlockCactus::onEntityCollide construye CONTACT, no una causa propia),
// por eso aca no hay CAUSE_CACTUS.
enum class DamageCause : int {
    CONTACT = 0,           // tocar fuego/cactus/lava desde un bloque
    ENTITY_ATTACK = 1,     // golpe cuerpo a cuerpo
    PROJECTILE = 2,        // flecha, bola de nieve...
    SUFFOCATION = 3,       // dentro de un bloque solido
    FALL = 4,
    FIRE = 5,              // parado sobre/entre fuego
    FIRE_TICK = 6,         // ardiendo
    LAVA = 7,
    DROWNING = 8,
    BLOCK_EXPLOSION = 9,
    ENTITY_EXPLOSION = 10, // creeper/TNT
    VOID = 11,
    SUICIDE = 12,
    MAGIC = 13,            // pocion de dano
    CUSTOM = 14,
    STARVATION = 15,
    LIGHTNING = 16,
};

// Genisys EntityDamageEvent::MODIFIER_*
enum class DamageModifier : int {
    BASE = 0,
    RESISTANCE = 1,
    ARMOR = 2,
    PROTECTION = 3,
    STRENGTH = 4,
    WEAKNESS = 5,
};

// Cantidad de slots de armadura del inventario del jugador (PlayerInventory
// ARMOR_OFFSET..+3: casco, pechera, pantalones, botas).
inline constexpr int kArmorSlotCount = 4;

class EntityDamageEvent : public Event, public Cancellable {
public:
    cursestone::entity::Entity* entity;

    // ($entity, $cause, $damage) — el orden de Genisys. `damage` es el
    // modificador BASE: un solo numero, o un array modificador->valor para
    // los eventos que nacen con varios (p.ej. el golpe con espada).
    EntityDamageEvent(cursestone::entity::Entity* e = nullptr,
                      DamageCause c = DamageCause::CUSTOM,
                      float damage = 0.0f)
        : entity(e), cause_(c) {
        modifiers_[key(DamageModifier::BASE)] = damage;
        originals_ = modifiers_;
        // Genisys: la Resistencia al Dano no se aplica a VOID ni a SUICIDE
        // (morir en el vacio tiene que doler aunque tengas pociones).
        if (cause_ != DamageCause::VOID && cause_ != DamageCause::SUICIDE &&
            e != nullptr && e->hasEffectId(cursestone::entity::EFF_RESISTANCE)) {
            float rate = 1.0f - 0.20f *
                (float)(e->effectAmplifier(cursestone::entity::EFF_RESISTANCE) + 1);
            if (rate < 0.0f) rate = 0.0f;
            setRateDamage(rate, DamageModifier::RESISTANCE);
        }
    }

    DamageCause cause() const { return cause_; }

    float getOriginalDamage(DamageModifier type = DamageModifier::BASE) const {
        auto it = originals_.find(key(type));
        return it == originals_.end() ? 0.0f : it->second;
    }
    float getDamage(DamageModifier type = DamageModifier::BASE) const {
        auto it = modifiers_.find(key(type));
        return it == modifiers_.end() ? 0.0f : it->second;
    }
    void setDamage(float damage, DamageModifier type = DamageModifier::BASE) {
        modifiers_[key(type)] = damage;
    }
    bool isApplicable(DamageModifier type) const {
        return modifiers_.find(key(type)) != modifiers_.end();
    }

    // Factor multiplicativo (1 = no cambia nada). Genisys devuelve 1 cuando
    // el modificador no esta seteado.
    float getRateDamage(DamageModifier type = DamageModifier::BASE) const {
        auto it = rate_modifiers_.find(key(type));
        return it == rate_modifiers_.end() ? 1.0f : it->second;
    }
    void setRateDamage(float rate, DamageModifier type = DamageModifier::BASE) {
        rate_modifiers_[key(type)] = rate;
    }

    // Genisys getFinalDamage(): el BASE multiplicado por TODOS los rate
    // modifiers y despues la suma de los modificadores aditivos que no son
    // BASE. No se recorta a 0 aca — el recorte es de setHealth(), igual que
    // en Genisys.
    float getFinalDamage() const {
        float damage = getDamage(DamageModifier::BASE);
        for (const auto& kv : rate_modifiers_) damage *= kv.second;
        for (const auto& kv : modifiers_) {
            if (kv.first != key(DamageModifier::BASE)) damage += kv.second;
        }
        return damage;
    }

    // ---- Armadura usada (Genisys EntityDamageEvent::$usedArmors) ----
    // El calculo de armadura/EPF vive en entity/damage/DamageCalculator.h y
    // anota aca que piezas participaron, para que despues del golpe se les
    // pueda gastar durabilidad (useArmors) sin recalcular nada.
    void markArmorUsed(int index) {
        if (index < 0 || index >= kArmorSlotCount) return;
        used_armors_[index] = 1;
    }
    bool isArmorUsed(int index) const {
        return index >= 0 && index < kArmorSlotCount && used_armors_[index] != 0;
    }

    // ---- Proteccion contra fuego / espinas (Genisys) ----
    // getFireProtectL() lo usa el atacante con Fire Aspect: si el defensor
    // tiene Fire Protection, la quemadura dura menos.
    int fireProtectLevel() const { return fire_protect_l_; }
    void setFireProtectLevel(int level) { fire_protect_l_ = level; }

    void setThornsLevel(int index, int level) {
        if (index < 0 || index >= kArmorSlotCount) return;
        thorns_levels_[index] = level;
    }
    const std::array<int, kArmorSlotCount>& thornsLevels() const { return thorns_levels_; }

    // Genisys createThornsDamage(): elige UNA pieza con espinas al azar y,
    // con probabilidad (nivel * 15)%, devuelve 1..4 de dano de vuelta al
    // atacante. Los dos aleatorios entran como parametros para que la
    // formula sea determinista en los tests.
    void createThornsDamage(int roll_1_100, int damage_1_4, int thorns_armor_index) {
        bool any = false;
        for (int lvl : thorns_levels_) if (lvl > 0) any = true;
        if (!any) return;
        thorns_armor_ = thorns_armor_index;
        int level = (thorns_armor_ >= 0 && thorns_armor_ < kArmorSlotCount)
                        ? thorns_levels_[thorns_armor_] : 0;
        if (roll_1_100 < level * 15) thorns_damage_ = damage_1_4;
    }
    int thornsDamage() const { return thorns_damage_; }
    // Genisys setThornsArmorUse(): la pieza que devolvio las espinas se
    // desgasta de mas (coste 3 en vez de 1).
    bool setThornsArmorUse() {
        if (thorns_armor_ < 0 || thorns_armor_ >= kArmorSlotCount) return false;
        used_armors_[thorns_armor_] = 3;
        return true;
    }
    int thornsArmorCost(int index) const {
        return (index >= 0 && index < kArmorSlotCount) ? used_armors_[index] : 0;
    }

    const std::string& eventName() const override {
        static std::string n = "EntityDamageEvent"; return n;
    }

protected:
    static int key(DamageModifier m) { return (int)m; }

    DamageCause cause_;
    std::map<int, float> modifiers_;       // modificadores aditivos (BASE incluido)
    std::map<int, float> rate_modifiers_;  // multiplicadores (sobre el BASE)
    std::map<int, float> originals_;       // valores tal como se construyo el evento
    std::array<int, kArmorSlotCount> used_armors_{{0, 0, 0, 0}};
    std::array<int, kArmorSlotCount> thorns_levels_{{0, 0, 0, 0}};
    int fire_protect_l_ = 0;
    int thorns_damage_ = 0;
    int thorns_armor_ = -1;
};

} // namespace cursestone::event::entity
