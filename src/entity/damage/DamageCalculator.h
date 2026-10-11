// DamageCalculator — la parte del sistema de dano de Genisys que NO vive
// dentro del evento: puntos de armadura, proteccion por encantamientos
// (EPF), espinas, empujon (knockback) y la formula de dano de caida.
//
// Portado del linaje Genisys 0.15:
//   * EntityDamageEvent::__construct   -> armadura + EPF + fireProtectL
//   * EntityDamageEvent::createThornsDamage
//   * Entity::fall / Entity::updateFallState
//   * Living::knockBack
//
// Deliberadamente header-only y SIN depender de Player/Inventory: quien
// llama entrega los numeros ya resueltos por slot de armadura (defensa +
// niveles de encantamiento). Eso permite testear toda la matematica en
// aislamiento (tools/damage_test.cpp) y que los aleatorios entren como
// parametros (en Genisys salen de mt_rand() adentro de la funcion, asi que
// serian imposibles de testear sin controlar el RNG).
#pragma once
#include "item/enchantment/Enchantment.h"
#include "event/entity/EntityDamageEvent.h"
#include <array>
#include <cmath>

namespace cursestone::entity::damage {

using event::entity::DamageCause;
using event::entity::DamageModifier;
using event::entity::EntityDamageEvent;
using event::entity::kArmorSlotCount;

// Una pieza de armadura puesta, ya resuelta desde
// (ItemStack -> ItemArmor::defensePoints() + encantamientos del stack).
struct ArmorPiece {
    int defense = 0;             // puntos de armadura del material
    int protection = 0;          // ENCH_PROTECTION
    int fire_protection = 0;     // ENCH_FIRE_PROTECTION
    int blast_protection = 0;    // ENCH_BLAST_PROTECTION
    int projectile_protection = 0;
    int feather_falling = 0;     // ENCH_FEATHER_FALLING (solo botas)
    int thorns = 0;              // ENCH_THORNS

    bool worn() const {
        return defense > 0 || protection > 0 || fire_protection > 0 ||
               blast_protection > 0 || projectile_protection > 0 ||
               feather_falling > 0 || thorns > 0;
    }
};

struct ArmorProfile {
    int points = 0;           // puntos de armadura totales (rate = 1 - 0.04 * points)
    int epf = 0;              // EPF ya tirado y topeado en 20
    int fire_protect_l = 0;   // nivel maximo de Fire Protection puesto
    int thorns_level = 0;     // nivel de espinas de la pieza elegida
    int thorns_armor = -1;    // indice de esa pieza (para el desgaste extra)
    bool any = false;         // alguna pieza participo del calculo

    // Genisys arma el rate con estos dos; los tiramos aca para que el
    // llamador solo tenga que hacer applyArmorModifiers().
    float armorRate()  const { return points != 0 ? 1.0f - 0.04f * (float)points : 1.0f; }
    float protectionRate() const { return epf != 0 ? 1.0f - 0.04f * (float)epf : 1.0f; }
};

// Genisys: el switch del constructor solo consume PUNTOS de armadura en
// estas causas. FALL y el resto no.
inline bool causeUsesArmorPoints(DamageCause c) {
    switch (c) {
        case DamageCause::CONTACT:
        case DamageCause::ENTITY_ATTACK:
        case DamageCause::PROJECTILE:
        case DamageCause::FIRE:
        case DamageCause::LAVA:
        case DamageCause::BLOCK_EXPLOSION:
        case DamageCause::ENTITY_EXPLOSION:
        case DamageCause::LIGHTNING:
            return true;
        default:
            return false;
    }
}

// Genisys: que encantamiento de proteccion especifico aplica a esta causa
// (y vale el DOBLE). -1 = ninguno.
inline int specificProtectionEnchant(DamageCause c) {
    switch (c) {
        case DamageCause::ENTITY_EXPLOSION:
        case DamageCause::BLOCK_EXPLOSION:
            return (int)item::enchantment::ENCH_BLAST_PROTECTION;
        case DamageCause::FIRE:
        case DamageCause::LAVA:
            return (int)item::enchantment::ENCH_FIRE_PROTECTION;
        case DamageCause::PROJECTILE:
            return (int)item::enchantment::ENCH_PROJECTILE_PROTECTION;
        default:
            return -1;
    }
}

inline int protectionLevelFor(const ArmorPiece& p, DamageCause cause) {
    switch (specificProtectionEnchant(cause)) {
        case (int)item::enchantment::ENCH_BLAST_PROTECTION:      return p.blast_protection;
        case (int)item::enchantment::ENCH_FIRE_PROTECTION:       return p.fire_protection;
        case (int)item::enchantment::ENCH_PROJECTILE_PROTECTION: return p.projectile_protection;
        default:                                                 return 0;
    }
}

// Genisys: EPF = min(20, ceil(EPF * mt_rand(50,100) / 100)).
inline int rollEpf(int raw_epf, int roll_50_100) {
    if (raw_epf <= 0) return 0;
    int rolled = (int)std::ceil((double)raw_epf * (double)roll_50_100 / 100.0);
    return rolled > 20 ? 20 : rolled;
}

// ============================================================
// computeArmorProfile — el bloque de armadura de
// EntityDamageEvent::__construct.
//
//   * causas con puntos: suma la defensa de TODAS las piezas puestas,
//     suma Protection de cada una, le agrega 2x la proteccion especifica
//     de la causa, y guarda el maximo Fire Protection (que despues usa
//     Fire Aspect) y el nivel de espinas de cada pieza.
//   * CAUSE_FALL: solo las BOTAS, con Protection + 3x Feather Falling.
//   * el resto: no toca armadura (fire tick, drowning, void, starving...).
// ============================================================
inline ArmorProfile computeArmorProfile(const std::array<ArmorPiece, kArmorSlotCount>& armor,
                                        DamageCause cause,
                                        int epf_roll_50_100,
                                        int thorns_pick = -1) {
    ArmorProfile out;
    if (causeUsesArmorPoints(cause)) {
        int raw_epf = 0;
        for (int i = 0; i < kArmorSlotCount; ++i) {
            const ArmorPiece& p = armor[(size_t)i];
            if (!p.worn()) continue;
            out.any = true;
            out.points += p.defense;
            if (p.fire_protection > out.fire_protect_l)
                out.fire_protect_l = p.fire_protection;
        }
        for (int i = 0; i < kArmorSlotCount; ++i) {
            const ArmorPiece& p = armor[(size_t)i];
            if (!p.worn()) continue;
            raw_epf += p.protection;
            raw_epf += 2 * protectionLevelFor(p, cause);
        }
        out.epf = rollEpf(raw_epf, epf_roll_50_100);
        // Espinas: Genisys elige UNA pieza al azar entre las que tienen
        // espinas (array_rand sobre $thornsLevel). El indice viene de
        // afuera; si no es valido, cae en la primera con espinas.
        if (thorns_pick < 0 || thorns_pick >= kArmorSlotCount ||
            armor[(size_t)thorns_pick].thorns <= 0) {
            thorns_pick = -1;
            for (int i = 0; i < kArmorSlotCount; ++i) {
                if (armor[(size_t)i].thorns > 0) { thorns_pick = i; break; }
            }
        }
        if (thorns_pick >= 0) {
            out.thorns_armor = thorns_pick;
            out.thorns_level = armor[(size_t)thorns_pick].thorns;
        }
    } else if (cause == DamageCause::FALL) {
        const ArmorPiece& boots = armor[3];   // ARMOR_SLOT_BOOTS
        if (boots.worn()) {
            out.any = true;
            out.epf = rollEpf(boots.protection + 3 * boots.feather_falling, epf_roll_50_100);
        }
    }
    return out;
}

// Anota el perfil en el evento (rate de armadura + rate de proteccion +
// fireProtectL). Los rates coexisten con RESISTANCE/STRENGTH/WEAKNESS: se
// multiplican todos juntos en getFinalDamage(), igual que en Genisys.
inline void applyArmorModifiers(EntityDamageEvent& ev, const ArmorProfile& profile) {
    if (profile.points != 0)
        ev.setRateDamage(profile.armorRate(), DamageModifier::ARMOR);
    if (profile.epf != 0)
        ev.setRateDamage(profile.protectionRate(), DamageModifier::PROTECTION);
    ev.setFireProtectLevel(profile.fire_protect_l);
}

// Genisys EntityDamageEvent::createThornsDamage — nivel * 15% de
// probabilidad de devolver 1..4 de dano.
inline int thornsDamageFor(int thorns_level, int roll_1_100, int damage_1_4) {
    if (thorns_level <= 0) return 0;
    return roll_1_100 < thorns_level * 15 ? damage_1_4 : 0;
}

// ============================================================
// Living::knockBack($attacker, $damage, $x, $z, $base)
//   x,z = (victima - atacante). La velocidad actual de la victima se
//   amortigua a la mitad, se le suma el empujon normalizado y el
//   componente vertical queda topeado en `base`.
// ============================================================
inline void knockBackMotion(double& mx, double& my, double& mz,
                            double x, double z, double base) {
    double f = std::sqrt(x * x + z * z);
    if (f <= 0.0) return;
    f = 1.0 / f;
    mx = mx / 2.0;
    my = my / 2.0;
    mz = mz / 2.0;
    mx += x * f * base;
    my += base;
    mz += z * f * base;
    if (my > base) my = base;
}

// ============================================================
// Entity::fall($fallDistance) + Entity::updateFallState()
//
// PORTADO DE LA REFERENCIA (SSMG64/PocketEdition, que es el codigo del
// propio cliente/servidor de MCPE),
// handheld/src/world/entity/Mob.cpp::causeFallDamage():
//
//     void Mob::causeFallDamage(float distance) {
//         int dmg = (int) ceil((distance - 3));
//         if (dmg > 0) { ...; hurt(NULL, dmg); }
//     }
//
// O sea: umbral de 3 bloques, ceil() y dano ENTERO (Mob::actuallyHurt(int)
// le resta a una salud entera). Antes esta base usaba floor(), que es de la
// familia PMMP/Genisys; con ceil() una caida de 3.05 bloques hace 1 de dano
// y una de 3.0 no hace nada, que es exactamente lo que hace la referencia.
//
// Se conservan dos cosas de la version de servidor porque la referencia no
// las modela y son vanilla en el 0.15: el descuento del efecto Jump Boost
// (1 bloque por nivel) y el bloque de slime debajo, que anula el dano por
// completo. Con las dos en cero esto es identico a la referencia.
// Fijado por tools/damage_test.cpp (seccion "dano de caida").
// ============================================================
inline double fallDamage(double fall_distance, int jump_boost_amplifier, bool slime_block_below) {
    if (slime_block_below) return 0.0;   // slime: anula el dano (vanilla)
    if (jump_boost_amplifier < 0) jump_boost_amplifier = 0;
    double damage = std::ceil(fall_distance - 3.0 - (double)jump_boost_amplifier);
    return damage > 0.0 ? damage : 0.0;
}

} // namespace cursestone::entity::damage
