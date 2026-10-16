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
// UpdateAttributesPacket — clientbound. Sincroniza los atributos de la
// entidad (salud, hambre...) para que el cliente renderice la barra de
// hambre del HUD.
//
// Wire format (protocol 84 — mirrors the reference server 0.15.10
// UpdateAttributesPacket BYTE-FOR-BYTE; NO varints in this protocol):
//   [Long BE]  eid (8 bytes, Binary::writeLong)
//   [Short BE] count (2 bytes)
//   for each attribute:
//     [Float BE] min
//     [Float BE] max
//     [Float BE] value
//     [String]   name (Short BE length + bytes — BinaryStream::putString)
// NOTE: there is NO default-value float on the protocol-84 wire and NO
// varints. Varint/LE encoding here desyncs the client parser (freeze on
// "Generating terrain" + app crash).
#pragma once

#include "DataPacket.h"
#include <string>
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class UpdateAttributesPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return UPDATE_ATTRIBUTES_PACKET;
    }

    struct Entry {
        float min = 0.0f;
        float max = 0.0f;
        float value = 0.0f;
        // Server-side only — NOT serialized on the protocol-84 wire (the
        // client derives defaults from its own attribute registry).
        float defaultValue = 0.0f;
        std::string name;
    };

    // The reference server: entityId as Int64 — 8-byte big-endian, NOT varint.
    uint64_t eid = 0;
    std::vector<Entry> entries;

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;
};

} // namespace cursestone::network::protocol
