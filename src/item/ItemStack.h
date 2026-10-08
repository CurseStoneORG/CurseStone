// ItemStack — lightweight value type used by Inventory / Tile
// containers. Holds (item_id, meta, count) plus the enchantment list.
// Conversion to/from the polymorphic Item* happens at the inventory
// boundary.
#pragma once
#include <cstdint>
#include "item/enchantment/EnchantmentList.h"
namespace cursestone::item {
struct ItemStack {
    uint16_t id = 0;
    uint8_t  meta = 0;
    int      count = 0;
    // Encantamientos del stack (el tag "ench" del NBT del item). Genisys los
    // lee del NBT; CurseStone todavia no carga ese tag, asi que la lista
    // arranca vacia — pero el sistema de dano YA la consulta (EPF de la
    // armadura, Knockback / Fire Aspect / Sharpness del arma), asi que en
    // cuanto el NBT de items se lea, los encantamientos empiezan a contar
    // sin tocar el pipeline.
    enchantment::EnchantmentList ench;
    bool isEmpty() const { return count <= 0 || id == 0; }
    static ItemStack EMPTY() { return {0, 0, 0, {}}; }
    // Nivel del encantamiento en este stack (0 = no lo tiene).
    int enchantmentLevel(enchantment::EnchantmentId e) const { return (int)ench.level(e); }
};
} // namespace cursestone::item
