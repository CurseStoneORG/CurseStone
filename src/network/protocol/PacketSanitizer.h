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
// PacketSanitizer.h — outbound byte validation for protocol-84 packets.
//
// A MCPE 0.15.10 client crashes on malformed wire data — a NaN position, a
// count field that exceeds the payload, a desynced metadata walk — and the
// only thing the operator sees is a dead device. This validator runs on the
// encoded bytes of every outbound packet (the exact bytes the client will
// parse) and reports the payload classes that are known to crash real
// clients, BEFORE they reach the wire. See docs/CRASH_DIAGNOSTICS.md.
//
// Every check below mirrors the matching encoder byte-for-byte; the packet
// header's "Wire format" comment is the source of truth. That is what makes
// dropping the packet on violation fail-safe: the validator can only miss a
// crasher, never invent one.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cursestone::network::protocol {

// One detected outbound-byte problem. Fixed-size fields, no allocation:
// this is filled on the packet hot path.
struct SanitizeViolation {
    bool violated = false;
    const char* field = "";   // "x", "motionY", "slot count", "metadata"...
    const char* problem = ""; // "NaN", "infinite", "walk out of bounds"...
    double value = 0.0;       // offending float value (0 when not numeric)
    uint32_t bits = 0;        // raw IEEE-754 bits of an offending float
    size_t offset = 0;        // byte offset inside the payload (after the pid)
};

// Validate an encoded packet payload (the bytes AFTER the leading pid byte).
// Returns a default-constructed (violated == false) record when clean.
// Cost: one switch on pid plus a handful of integer bit tests — branch-only,
// no FPU work, no allocation.
SanitizeViolation sanitizeOutbound(uint8_t pid, const uint8_t* payload, size_t len);

// First `max_bytes` payload bytes as lowercase hex with spaces, an ellipsis
// appended when the payload is longer. Diagnostics formatting helper shared
// with the forensics renderer; never called on the hot path.
std::string hexPreview(const uint8_t* payload, size_t len, size_t max_bytes = 64);

// Formats a suspect float for a log line: finite values print as decimal,
// NaN/Inf print both the class and the raw bits so the encoding bug that
// produced them is visible ("NaN(0x7fc01234)").
std::string formatSuspectFloat(double v, uint32_t bits);

} // namespace cursestone::network::protocol
