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
// LoginPacket — first packet from a client to a server.
// Carries: protocol version, edition (Pocket/Win10), chain JWT data,
// and the per-client "clientData" JWT (skin, device, language, etc.).
//
// Wire format (protocol 84):
//   [Uint8  ] protocol (84)
//   [Uint8  ] edition (0 = Pocket)
//   [Uint32 LE] chainData length
//   [N bytes ] chainData (JSON object: {"chain": [jwt1, jwt2, ...]})
//   [Uint32 LE] clientData JWT length
//   [N bytes ] clientData JWT
#pragma once

#include "DataPacket.h"
#include <string>
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class LoginPacket : public DataPacket {
public:
    uint8_t pid() const override { return LOGIN_PACKET; }
    bool canBeSentBeforeLogin() const override { return true; }

    void encode(std::vector<uint8_t>& out) const override;
    bool decode(const uint8_t* data, size_t len) override;

    // Public fields (populated by decode()).
    int       protocol = 0;
    uint8_t   edition = 0;            // 0 = Pocket
    std::string chainJson;            // raw chain data JSON
    std::string clientDataJwt;        // raw clientData JWT (3-dot-separated)
    // Parsed fields from the clientData JWT payload.
    std::string username;
    std::string clientUUID;
    std::string skinId;
    std::vector<uint8_t> skinData;
    std::string languageCode  = "en_US";
    std::string deviceModel   = "Standard";
    int         deviceOS      = -1;   // -1 = unknown
    int         clientId       = 0;
    // Why decode() gave up (empty on success). Surfaced in the console
    // log so an operator can tell a malformed/short buffer from a zlib
    // failure instead of only seeing "no session created".
    std::string decode_error;

private:
    // Parses the third segment of the clientData JWT (a JSON object).
    void parseClientDataPayload();
};

} // namespace cursestone::network::protocol
