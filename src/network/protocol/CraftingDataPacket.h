// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftingDataPacket — server→client recipe list (MCPE protocol 84).
//
// PMMP/Genisys wire format (CraftingDataPacket::encode):
//   putUnsignedVarInt(count(entries))
//   foreach entry:
//     putVarInt(entryType)
//     ...entry body...
//   putBool(cleanRecipes)
//
// Entry bodies:
//   ENTRY_SHAPELESS (0):
//     putUnsignedVarInt(ingredientCount); putSlot(each);
//     putUnsignedVarInt(1); putSlot(result);
//     putUUID(16 raw bytes)
//   ENTRY_SHAPED (1):
//     putVarInt(width); putVarInt(height);
//     putSlot(width*height celdas, fila por fila, SIN padding);
//     putUnsignedVarInt(1); putSlot(result);
//     putUUID(16 raw bytes)
//   ENTRY_FURNACE (2):  putVarInt(inputId); putSlot(result);
//   ENTRY_FURNACE_DATA (3):
//     putVarInt(inputId); putVarInt(inputDamage); putSlot(result);
//
// putSlot (BinaryStream::putSlot, protocolo 84):
//   id==0  → putVarInt(0)
//   else   → putVarInt(id) + putVarInt((damage << 8) | count)
//            + putLShort(nbtLen) + nbt   (nbtLen=0 acá)
#pragma once

#include "DataPacket.h"
#include "item/ItemStack.h"
#include "inventory/Recipe.h"
#include <vector>
#include <cstdint>

namespace cursestone::inventory { class CraftingManager; }

namespace cursestone::network::protocol {

// Una entrada de receta lista para serializar — copia ligera de
// inventory::Recipe (el manager puede recargarse mientras el packet
// está en cola de salida, así que el packet tiene su propia copia).
struct CraftingEntry {
    inventory::RecipeType type = inventory::RECIPE_SHAPED;
    cursestone::item::ItemStack result;
    std::array<uint16_t, 9> grid{};
    std::array<int16_t, 9> gridMeta{};
    std::vector<uint16_t> ingredients;
    std::vector<int16_t> ingredientMeta;
    int width = 0, height = 0;
    int16_t furnaceInputMeta = -1;
    std::array<uint8_t, 16> uuid{};
};

class CraftingDataPacket : public DataPacket {
public:
    uint8_t pid() const override { return CRAFTING_DATA_PACKET; }

    static constexpr int ENTRY_SHAPELESS = 0;
    static constexpr int ENTRY_SHAPED = 1;
    static constexpr int ENTRY_FURNACE = 2;
    static constexpr int ENTRY_FURNACE_DATA = 3;
    static constexpr int ENTRY_MULTI = 4;

    // Llena `entries` con TODAS las recetas del manager (llamar antes
    // de sendPacket).
    void addAllRecipes(const inventory::CraftingManager& mgr);

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;

private:
    std::vector<CraftingEntry> entries_;
    bool cleanRecipes = true;
};

} // namespace cursestone::network::protocol
