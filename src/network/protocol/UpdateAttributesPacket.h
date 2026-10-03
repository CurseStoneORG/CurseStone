// SPDX-License-Identifier: LGPL-3.0-or-later
// UpdateAttributesPacket — clientbound. Sincroniza los atributos de la
// entidad (salud, hambre...) para que el cliente renderice la barra de
// hambre del HUD.
//
// Wire format (protocol 84 — mirrors PMMP/Infinity UpdateAttributesPacket):
//   [VarU32] eid (unsigned varint, no zigzag)
//   [VarU32] count
//   for each attribute:
//     [Float LE] min
//     [Float LE] max
//     [Float LE] value
//     [Float LE] default
//     [String]   name (varint length + bytes)
#pragma once

#include "DataPacket.h"
#include <string>
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class UpdateAttributesPacket : public DataPacket {
public:
    uint8_t pid() const override { return UPDATE_ATTRIBUTES_PACKET; }

    struct Entry {
        float min = 0.0f;
        float max = 0.0f;
        float value = 0.0f;
        float defaultValue = 0.0f;
        std::string name;
    };

    // PMMP: putUnsignedVarInt(eid) — sin zigzag.
    uint64_t eid = 0;
    std::vector<Entry> entries;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
