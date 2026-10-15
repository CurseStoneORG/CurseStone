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
// BatchPacket — zlib-compressed envelope containing 0..N sub-packets.
//
// Wire format (protocol 84):
//   [1 byte: 0x06]
//   [Uint32 LE: compressed payload length]
//   [N bytes: zlib-compressed batch of sub-packets]
//
// Decompressed payload is a sequence of:
//   [Uint32 LE: sub-packet length]
//   [N bytes: sub-packet bytes]
// Each sub-packet starts with its own 1-byte packet ID.
#pragma once

#include "DataPacket.h"
#include <vector>
#include <cstdint>

namespace cursestone::network::protocol {

class BatchPacket : public DataPacket {
  public:
    uint8_t pid() const override {
        return BATCH_PACKET;
    }

    bool canBeSentBeforeLogin() const override {
        return true;
    }

    // Compress the given list of sub-packets into `compressed_`.
    void encode(std::vector<uint8_t>& out) const override;

    // Decompress `raw_` into individual sub-packets in `decoded_`.
    bool decode(const uint8_t* data, size_t len) override;

    // Add a complete (already-encoded with leading ID byte) sub-packet.
    // Length prefix is BE int32 (matches PMMP BatchPacket::encode -> putInt).
    void addSubPacket(const std::vector<uint8_t>& sub) {
        uint32_t n = static_cast<uint32_t>(sub.size());
        payload_.push_back((uint8_t)(n >> 24));
        payload_.push_back((uint8_t)(n >> 16));
        payload_.push_back((uint8_t)(n >> 8));
        payload_.push_back((uint8_t)(n));
        payload_.insert(payload_.end(), sub.begin(), sub.end());
    }

    const std::vector<uint8_t>& payload() const {
        return payload_;
    }
    void setPayload(std::vector<uint8_t> p) {
        payload_ = std::move(p);
    }

    // Compressed wire payload (for outgoing packets).
    const std::vector<uint8_t>& compressed() const {
        return compressed_;
    }
    void setCompressed(std::vector<uint8_t> c) {
        compressed_ = std::move(c);
    }

  private:
    std::vector<uint8_t> payload_;    // uncompressed sub-packets
    std::vector<uint8_t> compressed_; // zlib payload (no leading ID byte)
};

} // namespace cursestone::network::protocol
