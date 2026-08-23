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

namespace cenisyspe::network::protocol {

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

private:
    // Parses the third segment of the clientData JWT (a JSON object).
    void parseClientDataPayload();
};

} // namespace cenisyspe::network::protocol
