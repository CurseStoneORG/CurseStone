/*
 _____                     _____ _
/  __ \                   /  ___| |
| /  \/_   _ _ __ ___  ___\ `--.| |_ ___  _ __   ___
| |   | | | | '__/ __|/ _ \`--. \ __/ _ \| '_ \ / _ \
| \__/\ |_| | |  \__ \  __/\__/ / || (_) | | | |  __/
 \____/\__,_|_|  |___/\___\____/ \__\___/|_| |_|\___|

 * CurseStone | Open Source Server Software
 *
 * Copyright (C) 2026 CurseStoneORG
 *
 * Developed by CurseStoneORG.
 * Built for an open and extensible Minecraft server ecosystem.
 *
 * Licensed under the GNU Lesser General Public License v3.0 or later.
 * You may study, modify, and redistribute this software under its license.
 *
 * License: https://www.gnu.org/licenses/lgpl-3.0.html
 * Repository: https://github.com/CurseStoneORG/CurseStone
 *
 */
// SPDX-License-Identifier: LGPL-3.0-or-later
// AdvancedSourceInterface — adds per-player directDataPacket / putPacket
// / close to the base SourceInterface. Mirrors the reference server's
// AdvancedSourceInterface.
#pragma once

#include "SourceInterface.h"
#include <string>
#include <cstdint>
#include <vector>

namespace cursestone {

class Player;

namespace network {

// Wire-order classes — one RakNet ordering channel each. The packet mix a
// 0.15 client tolerates is strict about some sequences and indifferent to
// others, so every outbound send declares which class it belongs to:
//
//   - Unordered    → RELIABLE on channel 1. Everything whose inter-packet
//     order the client does not care about (movement, entity updates,
//     block events, chat). A lost datagram only retransmits itself.
//
//   - LoginOrdered → RELIABLE_ORDERED on channel 0. The login/spawn
//     handshake (PlayStatus → StartGame → SetTime → ...): the client
//     walks off and hangs if these arrive reordered, and the sequence is
//     short, so a retransmit-then-unblock here is never observed.
//
//   - ChunkOrdered → RELIABLE_ORDERED on channel 2. The chunk stream:
//     FullChunkDataPacket plus the BlockEntityDataPackets of its tiles.
//     The 0.15 client rebuilds a chunk when its payload arrives and
//     destroys every block actor it holds for that chunk, so a tile
//     packet processed BEFORE its chunk binds an actor the very next
//     datagram wipes — the chest then renders invisible and cannot open.
//     Plain RELIABLE delivery does NOT prevent this: real Wi-Fi/LTE
//     paths reorder UDP datagrams constantly, and a chunk payload spans
//     2-30 of them while each tile NBT fits in one. Sharing one ordered
//     channel turns tile-after-chunk into an on-the-wire guarantee
//     instead of a same-thread enqueueing accident that only holds on a
//     loopback network.
enum class WireStream : uint8_t {
    Unordered = 0,
    LoginOrdered = 1,
    ChunkOrdered = 2,
};

class AdvancedSourceInterface : public SourceInterface {
  public:
    // Send a packet (already-encoded bytes including the leading ID byte)
    // to the player identified by `ip`:`port`, routed through the given
    // WireStream (see the enum above for the reliability/channel table and
    // the client-side sequences each stream protects).
    // `data` is taken by value: callers that just built the buffer
    // (the batch writer) can std::move it in, so the wire payload
    // transfers into the outbound queue without a heap copy per send.
    virtual void sendRawPacket(const std::string& ip, uint16_t port,
                               std::vector<uint8_t> data,
                               WireStream stream = WireStream::Unordered) = 0;
    // Close a session immediately, optionally sending a DisconnectPacket first.
    virtual void closeSession(const std::string& ip, uint16_t port,
                              const std::string& reason = "") = 0;
};

} // namespace network
} // namespace cursestone
