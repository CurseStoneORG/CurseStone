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
// PacketForensics.h — per-player, always-on wire recorder for crash forensics.
//
// The global Diagnostics packet ring answers "what did the SERVER exchange
// lately" — shared by every player, so a busy server overwrites one client's
// tail within a tick. This recorder answers the question that actually
// matters after a client crash: WHAT DID THIS ONE CLIENT RECEIVE LAST, byte
// for byte. See docs/CRASH_DIAGNOSTICS.md.
//
// Recording is allocation-free: two fixed rings, raw 16-byte payload
// previews (hex formatting happens only when a dump is rendered), no
// strings, no heap. The same single thread writes the rings as the one
// that drains the batch queue in practice, so a plain mutex is plenty.
#pragma once

#include "network/protocol/PacketNames.h"

#include <cstdint>
#include <ctime>
#include <mutex>
#include <string>

namespace cursestone {
namespace diagnostics {

class PacketForensics {
  public:
    // Payload bytes kept per outbound record. Hex-formatted only at render
    // time, so raising this costs nothing on the hot path and 2 chars per
    // byte only inside dumps.
    static constexpr size_t kPreviewBytes = 16;
    static constexpr size_t kOutCapacity = 48;
    static constexpr size_t kInCapacity = 16;

    // One outbound wire frame: what the client was fed, when, and the head
    // of the payload. Fixed fields only — this struct is memcpy'd around.
    struct OutboundRecord {
        uint64_t wall_ms = 0; // epoch ms of the send
        uint32_t size = 0;    // full payload size (after the pid byte)
        uint8_t pid = 0;
        bool ordered = false;
        uint8_t preview_len = 0;
        uint8_t preview[kPreviewBytes] = {};
    };

    struct InboundRecord {
        uint64_t wall_ms = 0;
        uint32_t size = 0;
        uint8_t pid = 0;
    };

    // Record one outbound packet. `payload` points at the bytes AFTER the
    // leading pid byte (the same convention sanitizeOutbound uses); only
    // the first kPreviewBytes are copied.
    void recordOut(uint8_t pid, const uint8_t* payload, size_t len, bool ordered) {
        std::lock_guard<std::mutex> lk(mtx_);
        OutboundRecord& r = out_[out_head_];
        r.wall_ms = nowMs();
        r.size = len > 0xffffffffu ? 0xffffffffu : uint32_t(len);
        r.pid = pid;
        r.ordered = ordered;
        r.preview_len = uint8_t(len < kPreviewBytes ? len : kPreviewBytes);
        for (size_t i = 0; i < r.preview_len; ++i)
            r.preview[i] = payload[i];
        out_head_ = (out_head_ + 1) % kOutCapacity;
        if (out_count_ < kOutCapacity)
            ++out_count_;
        ++out_total_;
    }

    // Record one inbound packet (pid + size is enough: inbound payloads are
    // the client's own doing, the tail order is what correlates crashes).
    void recordIn(uint8_t pid, size_t len) {
        std::lock_guard<std::mutex> lk(mtx_);
        InboundRecord& r = in_[in_head_];
        r.wall_ms = nowMs();
        r.size = len > 0xffffffffu ? 0xffffffffu : uint32_t(len);
        r.pid = pid;
        in_head_ = (in_head_ + 1) % kInCapacity;
        if (in_count_ < kInCapacity)
            ++in_count_;
        ++in_total_;
    }

    // Epoch ms of the last outbound packet, or 0 when nothing was ever sent.
    uint64_t lastOutboundMs() const {
        std::lock_guard<std::mutex> lk(mtx_);
        if (out_count_ == 0)
            return 0;
        // head_ points at the slot that will be overwritten next, so the
        // newest record is the one just before it.
        return out_[(out_head_ + kOutCapacity - 1) % kOutCapacity].wall_ms;
    }

    // Lifetime counters (wrapped at 2^64; they are for the dumps only).
    uint64_t totalOut() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return out_total_;
    }
    uint64_t totalIn() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return in_total_;
    }

    // Multi-line tail for a dump file: newest last, PMMP-report style.
    //   [HH:MM:SS] OUT MovePlayer        34 B ordered  10 00 00 00 ...
    std::string renderOutboundTail(size_t max) const;
    std::string renderInboundTail(size_t max) const;

    // One-line summary for the console error line: the last few distinct
    // packets plus the age of the most recent one in milliseconds.
    //   "MovePlayer 34 B, FullChunkData 81234 B, SetEntityData 340 B (last sent 312 ms ago)"
    std::string summarizeOutbound() const;

  private:
    static uint64_t nowMs();

    mutable std::mutex mtx_;
    OutboundRecord out_[kOutCapacity];
    size_t out_head_ = 0; // next slot to overwrite
    size_t out_count_ = 0;
    InboundRecord in_[kInCapacity];
    size_t in_head_ = 0;
    size_t in_count_ = 0;
    uint64_t out_total_ = 0;
    uint64_t in_total_ = 0;
};

} // namespace diagnostics
} // namespace cursestone
