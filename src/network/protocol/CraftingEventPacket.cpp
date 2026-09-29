// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftingEventPacket — implementación (decode client→server).
//
// Formato verificado contra el CraftingEventPacket.php real de Genisys
// (protocolo 84) + BinaryStream::getSlot del mismo repo. Las listas de
// items están acotadas a 128 entradas por lado, igual que PMMP, para
// que un packet malicioso no fuerce un vector gigante.
#include "CraftingEventPacket.h"

namespace cursestone::network::protocol {
namespace {

// BinaryStream::getSlot: getVarInt(id); si <= 0 → item vacío; si no,
// getVarInt aux → damage = aux >> 8, count = aux & 0xff;
// getLShort nbtLen; skip nbt.
bool readSlot(PacketReader& r, cursestone::item::ItemStack& out) {
    uint32_t rawId = 0;
    if (!r.readVarU32(rawId)) return false;
    int32_t id = static_cast<int32_t>(rawId);
    // readVarU32 devuelve el valor SIN zigzag (unsigned). putSlot escribe
    // el id con putVarInt CON signo, pero los ids son positivos y caben,
    // así que el bit pattern es el mismo. (id <= 0 → vacío.)
    if (id <= 0 || id > 0xFFFF) {
        out = cursestone::item::ItemStack::EMPTY();
        return true;
    }
    uint32_t rawAux = 0;
    if (!r.readVarU32(rawAux)) return false;
    // aux fue escrito como varint con signo; el valor real puede ser
    // negativo (damage -1). Interpretar como signed.
    int32_t aux = static_cast<int32_t>(rawAux);
    int16_t damage = static_cast<int16_t>(aux >> 8);
    uint8_t count = static_cast<uint8_t>(aux & 0xFF);
    uint16_t nbtLen = 0;
    if (!r.readU16LE(nbtLen)) return false;
    if (nbtLen > 0 && !r.skipBytes(nbtLen)) return false;
    out.id = static_cast<uint16_t>(id);
    out.count = count;
    out.meta = static_cast<uint8_t>(damage & 0xFF);
    return true;
}

} // namespace

void CraftingEventPacket::encode(std::vector<uint8_t>&) const {
    // No-op — igual que PMMP: el server nunca manda este packet.
}

bool CraftingEventPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readU8(windowId)) return false;
    uint32_t rawType = 0;
    if (!r.readVarU32(rawType)) return false;
    type = static_cast<int32_t>(rawType);
    if (!r.readBytes(id.data(), 16)) return false;

    uint32_t n = 0;
    if (!r.readVarU32(n)) return false;
    input.clear();
    for (uint32_t i = 0; i < n && i < 128; ++i) {
        cursestone::item::ItemStack s;
        if (!readSlot(r, s)) return false;
        input.push_back(s);
    }

    if (!r.readVarU32(n)) return false;
    output.clear();
    for (uint32_t i = 0; i < n && i < 128; ++i) {
        cursestone::item::ItemStack s;
        if (!readSlot(r, s)) return false;
        output.push_back(s);
    }
    return true;
}

} // namespace cursestone::network::protocol
