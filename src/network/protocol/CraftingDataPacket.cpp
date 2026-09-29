// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftingDataPacket — implementación (encode server→client).
//
// Formato verificado contra el CraftingDataPacket.php real de Genisys
// (protocolo 84, MCPE 0.15.10) y BinaryStream::putSlot del mismo repo.
#include "CraftingDataPacket.h"
#include "inventory/CraftingManager.h"

namespace cursestone::network::protocol {
namespace {

// BinaryStream::putSlot fiel a protocolo 84:
//   id == 0            → putVarInt(0) y nada más
//   resto              → putVarInt(id) + putVarInt((damage<<8)|count)
//                        + putLShort(nbtLen) + nbt
// damage negativo (receta "any damage", -1) viaja sign-extendido en el
// varint con signo — igual que el Item::get(id, -1) de PMMP.
void putSlotRaw(PacketWriter& w, uint16_t id, int16_t meta, uint8_t count) {
    if (id == 0) {
        w.writeVarI32(0);
        return;
    }
    w.writeVarU32(id);
    int32_t aux = (static_cast<int32_t>(meta) << 8) | (count & 0xFF);
    w.writeVarI32(aux);
    w.writeU16LE(0);   // nbtLen = 0
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
    w.writeVarU32(static_cast<uint32_t>(entries_.size()));

    for (const auto& rec : entries_) {
        switch (rec.type) {
            case inventory::RECIPE_SHAPED: {
                w.writeVarI32(ENTRY_SHAPED);
                w.writeVarI32(rec.width);
                w.writeVarI32(rec.height);
                // Celdas fila por fila, SIN padding — exactamente
                // Width x Height. Requirement por celda = 1 (Nukkit/PMMP
                // fuerzan count 1 por ingrediente; el "Count: 9" del JSON
                // legacy de Nukkit es un artefacto del generador).
                const auto& grid = rec.grid;
                const auto& meta = rec.gridMeta;
                for (int row = 0; row < rec.height; ++row) {
                    for (int col = 0; col < rec.width; ++col) {
                        int i = row * 3 + col;
                        putSlotRaw(w, grid[i], meta[i], 1);
                    }
                }
                w.writeVarU32(1);
                putSlotRaw(w, rec.result.id,
                           static_cast<int16_t>(rec.result.meta),
                           static_cast<uint8_t>(rec.result.count));
                w.writeBytes(rec.uuid.data(), 16);
                break;
            }
            case inventory::RECIPE_SHAPELESS: {
                w.writeVarI32(ENTRY_SHAPELESS);
                const auto& ing = rec.ingredients;
                const auto& imeta = rec.ingredientMeta;
                w.writeVarU32(static_cast<uint32_t>(ing.size()));
                for (size_t i = 0; i < ing.size(); ++i) {
                    int16_t m = i < imeta.size() ? imeta[i] : 0;
                    putSlotRaw(w, ing[i], m, 1);
                }
                w.writeVarU32(1);
                putSlotRaw(w, rec.result.id,
                           static_cast<int16_t>(rec.result.meta),
                           static_cast<uint8_t>(rec.result.count));
                w.writeBytes(rec.uuid.data(), 16);
                break;
            }
            case inventory::RECIPE_FURNACE: {
                uint16_t inputId = rec.ingredients.empty() ? 0 : rec.ingredients[0];
                if (rec.furnaceInputMeta >= 0) {
                    w.writeVarI32(ENTRY_FURNACE_DATA);
                    w.writeVarI32(inputId);
                    w.writeVarI32(rec.furnaceInputMeta);
                } else {
                    w.writeVarI32(ENTRY_FURNACE);
                    w.writeVarI32(inputId);
                }
                putSlotRaw(w, rec.result.id,
                           static_cast<int16_t>(rec.result.meta),
                           static_cast<uint8_t>(rec.result.count));
                break;
            }
        }
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
