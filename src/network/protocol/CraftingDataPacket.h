/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneTeam
 *
 * Developed by CurseStoneTeam.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneTeam/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftingDataPacket — server→client recipe list (MCPE protocol 84).
//
// Wire format verificado BYTE A BYTE contra Dr1xyDev/Infinity
// (pocketmine/network/protocol/CraftingDataPacket.php, protocolo 84 /
// MCPE 0.15.10) y cross-checkeado con Nukkit 2016 (mismo formato):
//
//   putInt(BE)   count(entries)          <- Binary::writeInt = pack("N")
//   foreach entry:
//     putInt(BE)   entryType
//     putInt(BE)   strlen(body)          <- LENGTH-PREFIX por entrada
//     <body crudo>
//   putByte     cleanRecipes
//
// El length-prefix por entrada es CRÍTICO: el cliente lo usa para
// delimitar cada cuerpo y saltar entradas desconocidas. Sin él, el
// primer campo del cuerpo se interpreta como el type de la siguiente
// entrada y todo el packet se parsea como basura (kick/crash del APK).
//
// Entry bodies:
//   ENTRY_SHAPELESS (0):
//     putInt(BE) ingredientCount; putSlot(each);
//     putInt(BE) 1; putSlot(result);
//     putUUID(16 raw bytes)
//   ENTRY_SHAPED (1):
//     putInt(BE) width; putInt(BE) height;
//     putSlot(width*height celdas, fila-por-fila: z<Height exterior,
//             x<Width interior — idéntico a Genisys/Infinity, y el
//             patrón es fila-major anclado arriba-izquierda, como
//             Recipe::grid)
//     putInt(BE) 1; putSlot(result);
//     putUUID(16 raw bytes)
//   ENTRY_FURNACE (2):   putInt(BE) inputId; putSlot(result);
//   ENTRY_FURNACE_DATA (3):
//     putInt(BE) (inputId << 16) | inputDamage;   <- UN solo int, no dos
//     putSlot(result);
//
// putSlot (BinaryStream::putSlot p84 — shorts BIG-endian, no varints):
//   id==0  → putShort(0)                       (2 bytes, nada más)
//   resto  → putShort(id) + putByte(count) + putShort(damage)
//            + putLShort(nbtLen) + nbt   (nbtLen=0 acá; LShort sí es LE)
//   damage -1 ("any damage") viaja como 0xFFFF en el short BE.
//
// cleanRecipes: Infinity/Genisys lo mandan en true (generateRecipeList
// hace $pk->cleanRecipes = true). El cliente borra sus recetas por
// defecto y usa SOLO las nuestras — con la lista completa que manda el
// CraftingManager es lo correcto.
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
