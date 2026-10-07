// tools/damage_test.cpp
//
// Regression harness for the Genisys damage system ported into
// src/entity/damage/DamageCalculator.h + src/event/entity/EntityDamageEvent.h.
//
// Unlike tools/pickup_sim.cpp (which re-implements the numbers), this test
// INCLUDES the real headers and drives the delivered code, so it fails if
// someone changes the formulas:
//
//   g++ -std=c++20 -O0 -Isrc -o /tmp/damage_test tools/damage_test.cpp src/entity/Entity.cpp -pthread
//   /tmp/damage_test
//
// (or just tools/run_damage_test.sh)
//
// Every expected value below is the number the Genisys 0.15 formula
// produces, spelled out in the comment next to it.
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"
#include "entity/damage/DamageCalculator.h"
#include "entity/Effect.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace cursestone;
using event::entity::DamageCause;
using event::entity::DamageModifier;
using event::entity::EntityDamageEvent;

static int failures = 0;

static bool close_enough(double a, double b) {
    return std::fabs(a - b) < 1e-4;
}

static void check(const char* what, double got, double want) {
    const bool ok = close_enough(got, want);
    std::printf("  [%s] %-58s got=%.4f want=%.4f\n", ok ? " OK " : "FAIL", what, got, want);
    if (!ok) ++failures;
}

static void check_bool(const char* what, bool got, bool want) {
    const bool ok = (got == want);
    std::printf("  [%s] %-58s got=%s want=%s\n", ok ? " OK " : "FAIL", what,
                got ? "true" : "false", want ? "true" : "false");
    if (!ok) ++failures;
}

// A minimal Entity whose potion effects can be scripted, so the parts of the
// pipeline that ask the entity for its effects (Resistance in the event
// constructor, Strength/Weakness in the by-entity event) run for real.
struct FakeEntity : entity::Entity {
    int resistance_level = 0;   // 0 = sin efecto
    int strength_level = 0;
    int weakness_level = 0;

    FakeEntity() : entity::Entity(1, nullptr) {}

    bool hasEffectId(int id) const override {
        if (id == entity::EFF_RESISTANCE) return resistance_level > 0;
        if (id == entity::EFF_STRENGTH)   return strength_level > 0;
        if (id == entity::EFF_WEAKNESS)   return weakness_level > 0;
        return false;
    }
    int effectAmplifier(int id) const override {
        // Genisys guarda el AMPLIFIER (nivel - 1); las formulas usan
        // (amplifier + 1) = nivel real.
        if (id == entity::EFF_RESISTANCE && resistance_level > 0) return resistance_level - 1;
        if (id == entity::EFF_STRENGTH && strength_level > 0)     return strength_level - 1;
        if (id == entity::EFF_WEAKNESS && weakness_level > 0)     return weakness_level - 1;
        return 0;
    }
};

namespace dmg = entity::damage;

int main() {
    std::printf("Genisys damage system — regression\n");

    // ---------------------------------------------------------------
    // 1. Un evento sin nada: el dano final es el BASE.
    // ---------------------------------------------------------------
    std::printf("\n-- evento base --\n");
    {
        EntityDamageEvent ev(nullptr, DamageCause::FALL, 10.0f);
        check("sin modificadores: final = base", ev.getFinalDamage(), 10.0);
        check("getDamage(BASE)", ev.getDamage(), 10.0);
        check("getOriginalDamage(BASE)", ev.getOriginalDamage(), 10.0);
        check("getRateDamage sin setear = 1", ev.getRateDamage(), 1.0);
    }

    // ---------------------------------------------------------------
    // 2. Armadura (Genisys: rate = 1 - 0.04 * puntos).
    // ---------------------------------------------------------------
    std::printf("\n-- armadura: rate = 1 - 0.04 * points --\n");
    {
        std::array<dmg::ArmorPiece, event::entity::kArmorSlotCount> armor{};
        armor[0].defense = 3;   // casco
        armor[1].defense = 8;   // pechera
        armor[2].defense = 6;   // pantalones
        armor[3].defense = 3;   // botas
        auto p = dmg::computeArmorProfile(armor, DamageCause::ENTITY_ATTACK, 100);
        check("puntos totales = 20", p.points, 20);
        check("rate de armadura = 1 - 0.04*20 = 0.2", p.armorRate(), 0.2);
        EntityDamageEvent ev(nullptr, DamageCause::ENTITY_ATTACK, 10.0f);
        dmg::applyArmorModifiers(ev, p);
        check("dano final = 10 * 0.2 = 2", ev.getFinalDamage(), 2.0);
        check_bool("CAUSE_FALL no usa puntos de armadura",
                   dmg::causeUsesArmorPoints(DamageCause::FALL), false);
        check_bool("CAUSE_ENTITY_ATTACK si usa puntos",
                   dmg::causeUsesArmorPoints(DamageCause::ENTITY_ATTACK), true);
    }

    // ---------------------------------------------------------------
    // 3. EPF / Protection (Genisys: Protection 1x, proteccion especifica
    //    2x, Feather Falling 3x, tirada mt_rand(50,100), tope 20).
    // ---------------------------------------------------------------
    std::printf("\n-- EPF / Protection --\n");
    {
        std::array<dmg::ArmorPiece, event::entity::kArmorSlotCount> armor{};
        for (auto& a : armor) { a.defense = 1; a.protection = 4; }
        auto p100 = dmg::computeArmorProfile(armor, DamageCause::ENTITY_ATTACK, 100);
        check("EPF crudo = 4*4 = 16, tirada 100", p100.epf, 16);
        check("rate de proteccion = 1 - 0.04*16 = 0.36", p100.protectionRate(), 0.36);
        auto p50 = dmg::computeArmorProfile(armor, DamageCause::ENTITY_ATTACK, 50);
        check("misma armadura, tirada 50 -> ceil(16*0.5) = 8", p50.epf, 8);
        // CAUSE_FALL solo mira las BOTAS: Protection 4 + 3 * Feather 0 = 4.
        auto p0 = dmg::computeArmorProfile(armor, DamageCause::FALL, 100);
        check("CAUSE_FALL usa solo las botas: Protection 4 = 4", p0.epf, 4);
    }
    {
        // Fire: Protection + 2x Fire Protection de cada pieza.
        std::array<dmg::ArmorPiece, event::entity::kArmorSlotCount> armor{};
        for (auto& a : armor) { a.defense = 1; a.protection = 4; a.fire_protection = 4; }
        auto p = dmg::computeArmorProfile(armor, DamageCause::FIRE, 100);
        check("EPF fuego = 4*4 + 4*(2*4) = 48, topeado en 20", p.epf, 20);
        check("fireProtectL = 4", p.fire_protect_l, 4);
        auto pc = dmg::computeArmorProfile(armor, DamageCause::DROWNING, 100);
        check("ahogarse no toca armadura", pc.points, 0);
        check("ahogarse no acumula EPF", pc.epf, 0);
    }
    {
        // Botas solo: Protection + 3x Feather Falling.
        std::array<dmg::ArmorPiece, event::entity::kArmorSlotCount> armor{};
        armor[3].defense = 1;
        armor[3].protection = 3;
        armor[3].feather_falling = 2;
        auto p = dmg::computeArmorProfile(armor, DamageCause::FALL, 100);
        check("EPF caida = 3 + 3*2 = 9", p.epf, 9);
        check("rate caida = 1 - 0.04*9 = 0.64", p.protectionRate(), 0.64);
    }
    {
        check("rollEpf(0, 100) = 0", dmg::rollEpf(0, 100), 0);
        check("rollEpf(24, 100) -> tope 20", dmg::rollEpf(24, 100), 20);
        check("rollEpf(24, 50) = ceil(12) = 12", dmg::rollEpf(24, 50), 12);
    }

    // ---------------------------------------------------------------
    // 4. Multiplicadores acumulativos: armadura * proteccion * resistencia.
    // ---------------------------------------------------------------
    std::printf("\n-- multiplicadores acumulativos --\n");
    {
        std::array<dmg::ArmorPiece, event::entity::kArmorSlotCount> armor{};
        armor[1].defense = 5;      // 1 - 0.04*5 = 0.8
        armor[1].protection = 3;   // 1 - 0.04*3 = 0.88
        EntityDamageEvent ev(nullptr, DamageCause::ENTITY_ATTACK, 10.0f);
        dmg::applyArmorModifiers(ev, dmg::computeArmorProfile(armor, DamageCause::ENTITY_ATTACK, 100));
        check("10 * 0.8 * 0.88 = 7.04", ev.getFinalDamage(), 7.04);
        ev.setRateDamage(0.5f, DamageModifier::RESISTANCE);
        check("... * 0.5 = 3.52", ev.getFinalDamage(), 3.52);
        ev.setDamage(2.0f, DamageModifier::STRENGTH);   // modificador aditivo
        check("+ 2 aditivo (despues de los rates) = 5.52", ev.getFinalDamage(), 5.52);
    }

    // ---------------------------------------------------------------
    // 5. Resistencia al Dano (Genisys: 20% por nivel, en el constructor
    //    del evento, y NO aplica a VOID ni a SUICIDE).
    // ---------------------------------------------------------------
    std::printf("\n-- Resistencia al Dano --\n");
    {
        FakeEntity e;
        e.resistance_level = 1;   // rate 1 - 0.20*1 = 0.8
        EntityDamageEvent ev(&e, DamageCause::ENTITY_ATTACK, 10.0f);
        check("nivel 1: 10 * 0.8 = 8", ev.getFinalDamage(), 8.0);
        e.resistance_level = 5;   // rate 1 - 0.20*5 = 0 (no negativo)
        EntityDamageEvent ev5(&e, DamageCause::LAVA, 10.0f);
        check("nivel 5: rate 0 -> 0", ev5.getFinalDamage(), 0.0);
        e.resistance_level = 3;   // rate 1 - 0.6 = 0.4
        EntityDamageEvent ev_void(&e, DamageCause::VOID, 10.0f);
        check("VOID ignora la resistencia", ev_void.getFinalDamage(), 10.0);
        EntityDamageEvent ev_sui(&e, DamageCause::SUICIDE, 10.0f);
        check("SUICIDE ignora la resistencia", ev_sui.getFinalDamage(), 10.0);
    }

    // ---------------------------------------------------------------
    // 6. Strength / Weakness del atacante (Genisys
    //    EntityDamageByEntityEvent::addAttackerModifiers).
    // ---------------------------------------------------------------
    std::printf("\n-- Strength / Weakness del atacante --\n");
    {
        FakeEntity victim, attacker;
        {
            attacker.strength_level = 1;
            event::entity::EntityDamageByEntityEvent ev(
                &attacker, &victim, DamageCause::ENTITY_ATTACK, 10.0f);
            check("Strength I: 10 * 1.3 = 13", ev.getFinalDamage(), 13.0);
            check("knockBack por defecto = 0.4", ev.getKnockBack(), 0.4);
        }
        {
            attacker.strength_level = 0;
            attacker.weakness_level = 1;
            event::entity::EntityDamageByEntityEvent ev(
                &attacker, &victim, DamageCause::ENTITY_ATTACK, 10.0f);
            check("Weakness I: 10 * 0.8 = 8", ev.getFinalDamage(), 8.0);
        }
        {
            attacker.weakness_level = 2;      // rate 1 - 0.2*2 = 0.6
            event::entity::EntityDamageByEntityEvent ev(
                &attacker, &victim, DamageCause::ENTITY_ATTACK, 10.0f);
            check("Weakness II: 10 * 0.6 = 6", ev.getFinalDamage(), 6.0);
        }
        {
            attacker.weakness_level = 0;
            event::entity::EntityDamageByEntityEvent ev(
                &attacker, &victim, DamageCause::ENTITY_ATTACK, 10.0f, 0.7f);
            check("knockBack explicito (Knockback II) = 0.4 + 2*0.15", ev.getKnockBack(), 0.7);
        }
    }

    // ---------------------------------------------------------------
    // 7. Espinas (Genisys: nivel * 15% de devolver 1..4).
    // ---------------------------------------------------------------
    std::printf("\n-- espinas --\n");
    {
        EntityDamageEvent ev(nullptr, DamageCause::ENTITY_ATTACK, 5.0f);
        ev.setThornsLevel(1, 3);   // nivel 3 -> 45%
        ev.createThornsDamage(44, 2, 1);
        check("roll 44 < 45 con nivel 3 -> 2 de dano", ev.thornsDamage(), 2);
        EntityDamageEvent ev2(nullptr, DamageCause::ENTITY_ATTACK, 5.0f);
        ev2.setThornsLevel(1, 3);
        ev2.createThornsDamage(45, 2, 1);
        check("roll 45 (no < 45) -> 0", ev2.thornsDamage(), 0);
        EntityDamageEvent ev3(nullptr, DamageCause::ENTITY_ATTACK, 5.0f);
        ev3.createThornsDamage(1, 4, 0);
        check("sin Thorns en la armadura -> 0", ev3.thornsDamage(), 0);
        check("helper thornsDamageFor(nivel 4, roll 59) = 3",
              dmg::thornsDamageFor(4, 59, 3), 3);
        check("helper thornsDamageFor(nivel 4, roll 60) = 0",
              dmg::thornsDamageFor(4, 60, 3), 0);
    }

    // ---------------------------------------------------------------
    // 8. Knockback (Genisys Living::knockBack).
    // ---------------------------------------------------------------
    std::printf("\n-- knockback --\n");
    {
        double mx = 0, my = 0, mz = 0;
        dmg::knockBackMotion(mx, my, mz, 2.0, 0.0, 0.4);
        check("empujon en +X: 2 * 0.5 * 0.4 = 0.4", mx, 0.4);
        check("componente vertical = base (0.4)", my, 0.4);
        check("sin componente Z", mz, 0.0);
    }
    {
        // La velocidad previa se amortigua a la mitad y despues se le suma
        // el empujon; el resultado vertical queda TOPEADO en base.
        // 3.0 / 2 + 0.4 = 1.9 -> 0.4.
        double mx = 0, my = 3.0, mz = 0;
        dmg::knockBackMotion(mx, my, mz, 1.0, 0.0, 0.4);
        check("3.0/2 + 0.4 = 1.9, topeado en base -> 0.4", my, 0.4);
    }
    {
        // Cayendo: no hay tope inferior, asi que se ve la amortiguacion.
        double mx = 0, my = -3.0, mz = 0;
        dmg::knockBackMotion(mx, my, mz, 1.0, 0.0, 0.4);
        check("cayendo: -3.0/2 + 0.4 = -1.1", my, -1.1);
    }
    {
        double mx = 9, my = 9, mz = 9;
        dmg::knockBackMotion(mx, my, mz, 0.0, 0.0, 0.4);
        check("victima encima del atacante: sin empujon horizontal", mx, 9.0);
        check("... y sin cambio vertical", my, 9.0);
    }

    // ---------------------------------------------------------------
    // 9. Dano de caida (Genisys Entity::fall).
    // ---------------------------------------------------------------
    std::printf("\n-- dano de caida --\n");
    {
        check("caida de 10 bloques -> floor(10-3) = 7", dmg::fallDamage(10.0, 0, false), 7.0);
        check("caida de 3 bloques -> 0", dmg::fallDamage(3.0, 0, false), 0.0);
        check("caida de 3.9 -> floor(0.9) = 0", dmg::fallDamage(3.9, 0, false), 0.0);
        check("caida de 14.5 -> floor(11.5) = 11", dmg::fallDamage(14.5, 0, false), 11.0);
        check("Jump Boost II (nivel 2) -> floor(10-3-2) = 5",
              dmg::fallDamage(10.0, 2, false), 5.0);
        check("bloque de slime debajo -> 0", dmg::fallDamage(30.0, 0, true), 0.0);
    }

    std::printf("\n%s (%d failure(s))\n", failures ? "DAMAGE TEST: FAIL" : "DAMAGE TEST: OK",
                failures);
    return failures ? 1 : 0;
}
