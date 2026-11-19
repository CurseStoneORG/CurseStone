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
// PacketForensics.cpp — renderers for the per-player wire recorder.
#include "PacketForensics.h"

#include "network/protocol/PacketSanitizer.h"

#include <chrono>
#include <cstdio>

namespace cursestone {
namespace diagnostics {

uint64_t PacketForensics::nowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

namespace {

// Local HH:MM:SS stamp matching the Diagnostics renderers' style.
std::string shortStamp(uint64_t wall_ms) {
    std::time_t t = static_cast<std::time_t>(wall_ms / 1000);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
    return buf;
}

} // namespace

std::string PacketForensics::renderOutboundTail(size_t max) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (out_count_ == 0)
        return "(no outbound packets recorded)\n";
    const size_t oldest = (out_head_ + kOutCapacity - out_count_) % kOutCapacity;
    const size_t skip = out_count_ > max ? out_count_ - max : 0;
    std::string out;
    out.reserve(skip < out_count_ ? (out_count_ - skip) * 96 + 32 : 32);
    for (size_t j = skip; j < out_count_; ++j) {
        const OutboundRecord& r = out_[(oldest + j) % kOutCapacity];
        char head[96];
        std::snprintf(head, sizeof(head), "  [%s] OUT %-22s %u B %s",
                      shortStamp(r.wall_ms).c_str(), network::protocol::packetName(r.pid),
                      r.size, r.ordered ? "ordered" : "unordered");
        out += head;
        if (r.preview_len > 0) {
            out += "  ";
            out += network::protocol::hexPreview(r.preview, r.preview_len, r.preview_len);
        }
        out += "\n";
    }
    return out;
}

std::string PacketForensics::renderInboundTail(size_t max) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (in_count_ == 0)
        return "(no inbound packets recorded)\n";
    const size_t oldest = (in_head_ + kInCapacity - in_count_) % kInCapacity;
    const size_t skip = in_count_ > max ? in_count_ - max : 0;
    std::string out;
    out.reserve((in_count_ - skip) * 64 + 32);
    for (size_t j = skip; j < in_count_; ++j) {
        const InboundRecord& r = in_[(oldest + j) % kInCapacity];
        char line[96];
        std::snprintf(line, sizeof(line), "  [%s] IN  %-22s %u B\n",
                      shortStamp(r.wall_ms).c_str(), network::protocol::packetName(r.pid),
                      r.size);
        out += line;
    }
    return out;
}

std::string PacketForensics::summarizeOutbound() const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (out_count_ == 0)
        return "no outbound packet was ever recorded for this session";
    // Walk the newest 6 records backwards and compact consecutive
    // duplicates ("MovePlayer x12") — the movement stream otherwise drowns
    // the one big, suspicious packet the operator needs to see.
    const size_t oldest = (out_head_ + kOutCapacity - out_count_) % kOutCapacity;
    std::string out;
    size_t shown = 0;
    size_t j = out_count_;
    while (j > 0 && shown < 6) {
        --j;
        const OutboundRecord& r = out_[(oldest + j) % kOutCapacity];
        size_t run = 1;
        while (j > run && out_[(oldest + j - run) % kOutCapacity].pid == r.pid && run < 999)
            ++run;
        char piece[96];
        if (run > 1) {
            std::snprintf(piece, sizeof(piece), "%s x%zu (%u B each)", network::protocol::packetName(r.pid),
                          run, r.size);
            j -= run - 1;
        } else {
            std::snprintf(piece, sizeof(piece), "%s %u B", network::protocol::packetName(r.pid),
                          r.size);
        }
        if (shown > 0)
            out += ", ";
        out += piece;
        ++shown;
    }
    const uint64_t last_ms = out_[(out_head_ + kOutCapacity - 1) % kOutCapacity].wall_ms;
    const uint64_t age = nowMs() - last_ms;
    char tail[64];
    std::snprintf(tail, sizeof(tail), " — last sent %llu ms ago",
                  static_cast<unsigned long long>(age));
    out += tail;
    return out;
}

} // namespace diagnostics
} // namespace cursestone
