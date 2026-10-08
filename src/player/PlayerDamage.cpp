// PlayerDamage.cpp — el lado jugador del sistema de dano de Genisys:
//
//   * Player::attack($damage, EntityDamageEvent $source)
//   * Player::setHealth($amount)          (sincroniza el atributo HEALTH)
//   * el bloque de armadura / EPF de EntityDamageEvent::__construct
//   * el golpe cuerpo a cuerpo de Player::handleDataPacket (INTERACT_PACKET,
//     ACTION_LEFT_CLICK): dano del arma, Sharpness, Knockback, Fire Aspect,
//     espinas y agotamiento.
//
// Los numeros de armadura/EPF/espinas/knockback salen de
// entity/damage/DamageCalculator.h (header-only, testeado en
// tools/damage_test.cpp).
#include "Player.h"
#include "player/PlayerInternal.h"
#include "entity/damage/DamageCalculator.h"
#include "event/EventManager.h"
#include "event/entity/EntityDamageEvent.h"
#include "event/entity/EntityDamageByEntityEvent.h"
#include "event/entity/EntityCombustByEntityEvent.h"
#include "inventory/PlayerInventory.h"
#include "item/Item.h"
#include "item/ItemArmor.h"
#include "item/ItemStack.h"
#include <array>
#include <cmath>
#include <cstdlib>

namespace cursestone {

using event::entity::DamageCause;
using event::entity::DamageModifier;
using event::entity::EntityDamageEvent;

// ============================================================
// Genisys Player::setHealth($amount):
//     parent::setHealth($amount);
//     if($this->spawned === true){
//         $this->foodTick = 0;
//         $this->getAttributeMap()->getAttribute(HEALTH)
//              ->setMaxValue($this->getMaxHealth())->setValue($amount, true);
//     }
// El setValue(..., true) es lo que mueve los corazones EN EL ACTO; el
// 0.15.x lee el atributo generic.health, no el SetHealthPacket.
// ============================================================
void Player::setHealth(float amount) {
    entity::Living::setHealth(amount);
    if (spawned_.load(std::memory_order_relaxed)) {
        food_tick_timer_ = 0;
        sendHealthAttribute();
    }
}

int Player::worldDifficulty() const {
    return srv_ ? srv_->difficulty() : 1;
}

// ============================================================
// Bloque de armadura de Genisys EntityDamageEvent::__construct.
// Resuelve el inventario de armadura a numeros (defensa + niveles de
// encantamiento) y le pide el perfil a DamageCalculator, que es quien
// aplica los rate modifiers de ARMOR y PROTECTION, el Fire Protection y
// los niveles de espinas.
//
// Aleatorios, como en Genisys:
//   * EPF: min(20, ceil(EPF * mt_rand(50,100) / 100))
//   * espinas: array_rand sobre las piezas con THORNS
// ============================================================
static void buildArmorProfile(Player& player, EntityDamageEvent& ev) {
    std::array<entity::damage::ArmorPiece, event::entity::kArmorSlotCount> pieces{};
    auto inv = player.getInventory();
    if (inv) {
        for (int i = 0; i < event::entity::kArmorSlotCount; ++i) {
            item::ItemStack st = inv->getArmorItem(i);
            if (st.isEmpty()) continue;
            item::Item* it = item::Item::get(st.id);
            if (it == nullptr || !it->isArmor()) continue;
            auto* armor = static_cast<item::ItemArmor*>(it);
            entity::damage::ArmorPiece& piece = pieces[(size_t)i];
            piece.defense = armor->defensePoints();
            piece.protection = st.enchantmentLevel(item::enchantment::ENCH_PROTECTION);
            piece.fire_protection = st.enchantmentLevel(item::enchantment::ENCH_FIRE_PROTECTION);
            piece.blast_protection = st.enchantmentLevel(item::enchantment::ENCH_BLAST_PROTECTION);
            piece.projectile_protection =
                st.enchantmentLevel(item::enchantment::ENCH_PROJECTILE_PROTECTION);
            piece.feather_falling = st.enchantmentLevel(item::enchantment::ENCH_FEATHER_FALLING);
            piece.thorns = st.enchantmentLevel(item::enchantment::ENCH_THORNS);
        }
    }

    const int epf_roll = 50 + (std::rand() % 51);                        // mt_rand(50, 100)
    const int thorns_pick = std::rand() % event::entity::kArmorSlotCount;
    auto profile = entity::damage::computeArmorProfile(
        pieces, ev.cause(), epf_roll, thorns_pick);
    entity::damage::applyArmorModifiers(ev, profile);

    // Piezas que participaron del calculo -> el evento las recuerda para
    // poder gastarles durabilidad despues del golpe (Genisys useArmors).
    if (entity::damage::causeUsesArmorPoints(ev.cause())) {
        for (int i = 0; i < event::entity::kArmorSlotCount; ++i) {
            if (pieces[(size_t)i].worn()) ev.markArmorUsed(i);
        }
    }
    // Niveles de espinas por pieza: los necesita EntityDamageEvent::
    // createThornsDamage(), que el ATACANTE llama despues de golpear.
    for (int i = 0; i < event::entity::kArmorSlotCount; ++i) {
        if (pieces[(size_t)i].thorns > 0) ev.setThornsLevel(i, pieces[(size_t)i].thorns);
    }
}

// ============================================================
// Player::attack($damage, EntityDamageEvent $source)
// ============================================================
bool Player::attack(float damage, EntityDamageEvent& source) {
    if (!isAlive()) return false;

    // Genisys Player::attack: el creativo solo recibe magia, suicidio y
    // vacio; con vuelo permitido el dano de caida se cancela.
    const DamageCause cause = source.cause();
    if (isCreative() && cause != DamageCause::MAGIC &&
        cause != DamageCause::SUICIDE && cause != DamageCause::VOID) {
        source.setCancelled();
    } else if (getAllowFlight() && cause == DamageCause::FALL) {
        source.setCancelled();
    }

    // El bloque de armadura corre ANTES del dispatch: un listener que mire
    // getFinalDamage() ve el dano ya reducido, igual que en Genisys.
    if (!source.isCancelled()) buildArmorProfile(*this, source);

    if (!entity::Living::attack(damage, source)) return false;

    // Genisys Player::attack: recibir un golpe cuesta 0.3 de agotamiento
    // (en supervivencia).
    if (isSurvival()) exhaust(0.3f);
    return true;
}

// ============================================================
// Golpe cuerpo a cuerpo — Genisys Player::handleDataPacket
// (case INTERACT_PACKET, ACTION_LEFT_CLICK).
//
// El orden importa: el evento se arma con el dano BASE del item y el
// knockBack del encantamiento; la victima lo procesa (armadura, efectos,
// invulnerabilidad); recien con el golpe aplicado el atacante dispara Fire
// Aspect, tira las espinas de la victima y paga 0.3 de agotamiento.
// ============================================================
bool Player::meleeAttack(Player& target, bool cancelled) {
    item::ItemStack held = inv_ ? inv_->getHeldItem() : item::ItemStack::EMPTY();
    item::Item* item = held.isEmpty() ? nullptr : item::Item::get(held.id);

    // Genisys Item::getAttackDamage() (1 en mano vacia) +
    // getModifyAttackDamage(): 0.5 por nivel (+1) de Sharpness.
    float base_damage = BASE_ATTACK_DAMAGE;   // mano vacia = 1 (Item::getAttackDamage)
    if (item != nullptr && item->attackDamage() > 0) {
        base_damage = (float)item->attackDamage();
    }
    const int sharpness = held.enchantmentLevel(item::enchantment::ENCH_SHARPNESS);
    if (sharpness > 0) base_damage += 0.5f * (float)(sharpness + 1);

    // Genisys: knockBack = 0.4 + 0.15 por nivel de Knockback del arma.
    const float knock_back =
        0.4f + 0.15f * (float)held.enchantmentLevel(item::enchantment::ENCH_KNOCKBACK);

    event::entity::EntityDamageByEntityEvent ev(
        this, &target, DamageCause::ENTITY_ATTACK, base_damage, knock_back);
    if (cancelled) ev.setCancelled();

    const bool applied = target.attack(ev.getFinalDamage(), ev);

    if (applied) {
        // Fire Aspect: 4 s de quemadura por nivel. La Fire Protection de la
        // victima la recorta (Genisys le pasa getFireProtectL() al evento).
        const int fire_aspect = held.enchantmentLevel(item::enchantment::ENCH_FIRE_ASPECT);
        if (fire_aspect > 0) {
            int seconds = fire_aspect * 4;
            const int protect = ev.fireProtectLevel();
            if (protect > 0) seconds = (seconds * (100 - 15 * protect)) / 100;
            auto fire_ev = event::EventManager::get()
                               .fire<event::entity::EntityCombustByEntityEvent>(
                                   &target, seconds, this);
            if (!fire_ev.isCancelled()) target.setOnFire(fire_ev.seconds);
        }

        // Espinas: la armadura de la victima devuelve 1..4 de dano con
        // probabilidad (nivel * 15)%. Los dos aleatorios son de Genisys
        // (mt_rand(1,100) y mt_rand(1,4)).
        if (isSurvival()) {
            ev.createThornsDamage(1 + (std::rand() % 100),
                                  1 + (std::rand() % 4),
                                  std::rand() % event::entity::kArmorSlotCount);
            if (ev.thornsDamage() > 0) {
                event::entity::EntityDamageByEntityEvent thorns_ev(
                    &target, this, DamageCause::ENTITY_ATTACK,
                    (float)ev.thornsDamage(), 0.0f);   // knockBack 0 en espinas
                if (attack((float)ev.thornsDamage(), thorns_ev)) {
                    thorns_ev.setThornsArmorUse();
                }
            }
        }
    }

    if (ev.isCancelled()) return false;

    // Genisys (rama isSurvival): desgasta el arma (Tool::useOn) y paga el
    // agotamiento del ataque. CurseStone todavia no guarda la durabilidad
    // del item en el stack, asi que el desgaste del arma queda pendiente
    // (ver limitaciones); el agotamiento si se aplica.
    if (isSurvival()) exhaust(0.3f);
    return true;
}

} // namespace cursestone
