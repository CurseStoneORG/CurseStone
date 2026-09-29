// SPDX-License-Identifier: LGPL-3.0-or-later
// CraftingEventPacket — implementación (decode client→server).
//
// Formato verificado contra el CraftingEventPacket.php de
// Dr1xyDev/Infinity (protocolo 84) + Nukkit 2016 — p84 usa getInt BE y
// slots de shorts BE, NO varints:
//   getByte()      windowId
//   getInt(BE)     type
//   get(16)        id (UUID binario de la receta)
//   getInt(BE) size; getSlot() × size    (input, máx 128)
//   getInt(BE) size; getSlot() × size    (output, máx 128)
//
// getSlot p84 (Infinity, simétrico al putSlot de CraftingDataPacket):
//   id = getShort BE; si id <= 0 → slot vacío y NO se lee nada más;
//   si no → getByte count + getShort BE damage + getLShort LE nbtLen
//   + nbt.
//
// Las listas de items están acotadas a 128 entradas por lado, igual
// que PMMP/Nukkit, para que un packet malicioso no fuerce un vector
// gigante.
#include "CraftingEventPacket.h"

namespace cursestone::network::protocol {
namespace {

// BinaryStream::getSlot de protocolo 84 (formato shorts BE).
bool readSlot(PacketReader& r, cursestone::item::ItemStack& out) {
    int16_t id = 0;
    if (!r.readI16BE(id)) return false;
    if (id <= 0) {
        // Slot vacío: solo los 2 bytes del short — el cliente tampoco
        // escribe count/damage/nbt para slots vacíos.
        out = cursestone::item::ItemStack::EMPTY();
        return true;
    }
    uint8_t count = 0;
    if (!r.readU8(count)) return false;
    uint16_t damageRaw = 0;
    if (!r.readU16BE(damageRaw)) return false;
    uint16_t nbtLen = 0;
    if (!r.readU16LE(nbtLen)) return false;   // getLShort = LE
    if (nbtLen > 0 && !r.skipBytes(nbtLen)) return false;
    out.id = static_cast<uint16_t>(id);
    out.count = count;
    out.meta = static_cast<uint8_t>(static_cast<int16_t>(damageRaw) & 0xFF);
    return true;
}

} // namespace

void CraftingEventPacket::encode(std::vector<uint8_t>&) const {
    // No-op — igual que PMMP/Infinity: el server nunca manda este packet.
}

bool CraftingEventPacket::decode(const uint8_t* data, size_t len) {
    PacketReader r(data, len);
    if (!r.readU8(windowId)) return false;
    if (!r.readI32BE(type)) return false;     // getInt = BE con signo
    if (!r.readBytes(id.data(), 16)) return false;

    int32_t n = 0;
    if (!r.readI32BE(n) || n < 0) return false;
    input.clear();
    for (int32_t i = 0; i < n && i < 128; ++i) {
        cursestone::item::ItemStack s;
        if (!readSlot(r, s)) return false;
        input.push_back(s);
    }

    if (!r.readI32BE(n) || n < 0) return false;
    output.clear();
    for (int32_t i = 0; i < n && i < 128; ++i) {
        cursestone::item::ItemStack s;
        if (!readSlot(r, s)) return false;
        output.push_back(s);
    }
    return true;
}

} // namespace cursestone::network::protocol
