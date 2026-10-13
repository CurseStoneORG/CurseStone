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
// CraftingDataPacket — implementación (encode server→client).
//
// Formato verificado BYTE A BYTE contra el CraftingDataPacket.php real
// de Dr1xyDev/Infinity (protocolo 84, MCPE 0.15.10), cross-checkeado
// con Nukkit 2016 (mismo formato de la era) y Genisys para la
// orientación de datos de las recetas (fila-por-fila, anclado
// arriba-izquierda).
//
// Resumen del wire format (ver CraftingDataPacket.h):
//   putInt BE(count) — luego POR CADA entrada: putInt BE(type),
//   putInt BE(bodyLen), body crudo — putByte(cleanRecipes).
//   putSlot usa shorts BE: id, count (byte), damage, nbtLen (LE).
//
// BUG FIX (crash del APK al unirse): la versión anterior escribía
// varints y omitía el length-prefix por entrada — un formato que no
// existe en el protocolo 84. El cliente delimitaba cada entrada con
// su longitud real, leía el primer campo del body como si fuera el
// type de la siguiente entrada y el packet entero se parseaba como
// basura → kick/crash total del APK al llegar el packet en el spawn.
#include "CraftingDataPacket.h"
#include "inventory/CraftingManager.h"

namespace cursestone::network::protocol {
namespace {

// BinaryStream::putSlot fiel a protocolo 84 (Infinity putSlot):
//   id == 0            → putShort(0) y nada más
//   resto              → putShort(id) + putByte(count) + putShort(damage)
//                        + putLShort(nbtLen) + nbt
// damage negativo (receta "any damage", -1) viaja como 0xFFFF en el
// short BE — exactamente lo que Item::get(id, -1) produce en PMMP.
void putSlotRaw(PacketWriter& w, uint16_t id, int16_t meta, uint8_t count) {
    if (id == 0) {
        w.writeI16BE(0);
        return;
    }
    w.writeI16BE(static_cast<int16_t>(id));
    w.writeU8(count);
    w.writeI16BE(meta);          // -1 → 0xFFFF, igual que PMMP
    w.writeU16LE(0);             // nbtLen = 0 (putLShort = LE)
}

// BinaryStream::putInt — 4 bytes BIG-endian con signo (pack("N")).
void putIntBE(PacketWriter& w, int32_t v) {
    w.writeI32BE(v);
}

} // namespace

void CraftingDataPacket::addAllRecipes(const inventory::CraftingManager& mgr) {
    entries_.clear();
    entries_.reserve(mgr.recipes().size());
    for (const auto& r : mgr.recipes()) {
        if (!r) continue;
        CraftingEntry e;
        e.type = r->type();
        e.result = r->result();
        e.grid = r->grid();
        e.gridMeta = r->gridMeta();
        e.ingredients = r->ingredients();
        e.ingredientMeta = r->ingredientMeta();
        e.width = r->width();
        e.height = r->height();
        e.furnaceInputMeta = r->furnaceInputMeta();
        e.uuid = r->uuidBytes();
        entries_.push_back(std::move(e));
    }
}

void CraftingDataPacket::encode(std::vector<uint8_t>& out) const {
    PacketWriter w(out);
    putIntBE(w, static_cast<int32_t>(entries_.size()));

    // Body de una entrada — se escribe a un stream temporal para poder
    // anteponer (type, length) por entrada, igual que el $writer
    // temporal del encode() de Infinity.
    std::vector<uint8_t> body;

    for (const auto& rec : entries_) {
        body.clear();
        PacketWriter bw(body);

        // OJO: el enum interno RecipeType (SHAPED=0/SHAPELESS=1) está
        // INVERTIDO respecto a las constantes del wire (ENTRY_SHAPELESS=0/
        // ENTRY_SHAPED=1) — mapear explícitamente, nunca cast directo.
        int32_t entryType = -1;

        switch (rec.type) {
            case inventory::RECIPE_SHAPED: {
                entryType = ENTRY_SHAPED;
                putIntBE(bw, rec.width);
                putIntBE(bw, rec.height);
                // Celdas fila-por-fila: z<Height exterior, x<Width
                // interior (Genisys/Infinity). Recipe::grid está anclado
                // arriba-izquierda en fila-major (row*3+col), así que el
                // índice lineal es exactamente row*width + col SIN
                // padding — Width x Height celdas exactas. Requirement
                // por celda = 1 (el "Count: 9" del JSON legacy de Nukkit
                // es un artefacto del generador; PMMP también fuerza 1).
                const auto& grid = rec.grid;
                const auto& meta = rec.gridMeta;
                for (int row = 0; row < rec.height; ++row) {
                    for (int col = 0; col < rec.width; ++col) {
                        int i = row * 3 + col;
                        putSlotRaw(bw, grid[i], meta[i], 1);
                    }
                }
                putIntBE(bw, 1);
                putSlotRaw(bw, rec.result.id,
                           static_cast<int16_t>(rec.result.meta),
                           static_cast<uint8_t>(rec.result.count));
                bw.writeBytes(rec.uuid.data(), 16);
                break;
            }
            case inventory::RECIPE_SHAPELESS: {
                entryType = ENTRY_SHAPELESS;
                const auto& ing = rec.ingredients;
                const auto& imeta = rec.ingredientMeta;
                putIntBE(bw, static_cast<int32_t>(ing.size()));
                for (size_t i = 0; i < ing.size(); ++i) {
                    int16_t m = i < imeta.size() ? imeta[i] : 0;
                    putSlotRaw(bw, ing[i], m, 1);
                }
                putIntBE(bw, 1);
                putSlotRaw(bw, rec.result.id,
                           static_cast<int16_t>(rec.result.meta),
                           static_cast<uint8_t>(rec.result.count));
                bw.writeBytes(rec.uuid.data(), 16);
                break;
            }
            case inventory::RECIPE_FURNACE: {
                uint16_t inputId = rec.ingredients.empty() ? 0 : rec.ingredients[0];
                if (rec.furnaceInputMeta >= 0) {
                    entryType = ENTRY_FURNACE_DATA;
                    // ENTRY_FURNACE_DATA: (id << 16) | damage como UN
                    // solo int BE (writeFurnaceRecipe de Infinity).
                    putIntBE(bw, (static_cast<int32_t>(inputId) << 16) |
                                     static_cast<int32_t>(rec.furnaceInputMeta));
                } else {
                    entryType = ENTRY_FURNACE;
                    putIntBE(bw, inputId);
                }
                putSlotRaw(bw, rec.result.id,
                           static_cast<int16_t>(rec.result.meta),
                           static_cast<uint8_t>(rec.result.count));
                break;
            }
            default:
                continue;   // tipo desconocido — no mandar la entrada
        }

        putIntBE(w, entryType);
        putIntBE(w, static_cast<int32_t>(body.size()));
        w.writeBytes(body.data(), body.size());
    }

    w.writeU8(cleanRecipes ? 1 : 0);
}

bool CraftingDataPacket::decode(const uint8_t* data, size_t len) {
    // El server nunca decodifica este packet, pero PacketPool sí llama
    // decode() al recibirlo — parsear el conteo y descartar el resto
    // sin fallar.
    (void)data;
    (void)len;
    return true;
}

} // namespace cursestone::network::protocol
