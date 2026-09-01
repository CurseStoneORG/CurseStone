// SPDX-License-Identifier: LGPL-3.0-or-later
// Session.c — per-client RakNet state machine, Protocol 84 / MCPE 0.15.10
// Ported from raklib/server/Session.php (PocketMine-MP 4.0.0 legacy branch)
#include "Session.h"
#include "RakLib.h"
#include "../../utils/Utils.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// --------------------------------------------------------------------------
// Init / Destroy
// --------------------------------------------------------------------------
void mcp_rak_session_init(mcp_rak_session_t* s, const uint8_t ip[4],
                           uint16_t port, int64_t client_id, int64_t server_id) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (ip) memcpy(s->ip, ip, 4);
    s->port         = port;
    s->client_id    = client_id;
    s->server_id    = server_id;
    s->state        = MCP_SESSION_STATE_UNCONNECTED;
    s->mtu          = RAKLIB_MAX_MTU;
    s->last_recv_seq = -1;
    s->window_start = 0;
    s->window_end   = MCP_SESSION_WINDOW_SIZE;
    s->last_activity_ms = mcp_utils_time_ms();
    s->start_time_ms    = s->last_activity_ms;
    s->is_active    = false;
    s->is_disconnecting = false;  // Bug 2 fix
    for (int i = 0; i < 32; ++i) s->channel_index[i] = 0;
    s->split.in_use = false;
}

void mcp_rak_session_destroy(mcp_rak_session_t* s) {
    if (!s) return;
    // Free send queue
    for (size_t i = 0; i < s->send_queue_count; ++i) free(s->send_queue_bufs[i]);
    free(s->send_queue_bufs);
    free(s->send_queue_lens);
    // Free recovery queue
    for (size_t i = 0; i < MCP_RECOVERY_CAP; ++i) {
        if (s->recovery[i].data) free(s->recovery[i].data);
    }
    // Free split store segments
    if (s->split.in_use) {
        for (uint32_t i = 0; i < s->split.count && i < MCP_MAX_SPLIT_SEGMENTS; ++i) {
            free(s->split.segments[i]);
        }
    }
    memset(s, 0, sizeof(*s));
}

// --------------------------------------------------------------------------
// ACK / NACK queues
// --------------------------------------------------------------------------
void mcp_rak_session_queue_ack(mcp_rak_session_t* s, uint32_t seq) {
    if (!s || s->ack_queue_len >= MCP_MAX_ACK_QUEUE) return;
    if (s->ack_queue_len > 0 && s->ack_queue[s->ack_queue_len - 1] == seq) return;
    s->ack_queue[s->ack_queue_len++] = seq;
}

void mcp_rak_session_queue_nack(mcp_rak_session_t* s, uint32_t seq) {
    if (!s || s->nack_queue_len >= MCP_MAX_NACK_QUEUE) return;
    // dedup
    for (size_t i = 0; i < s->nack_queue_len; ++i)
        if (s->nack_queue[i] == seq) return;
    s->nack_queue[s->nack_queue_len++] = seq;
}

// Collapse a sorted array of seqs into (min,max) ranges.
static size_t collapse_ranges(const uint32_t* seqs, size_t len,
                               uint32_t* mins, uint32_t* maxs, size_t cap) {
    if (!len) return 0;
    size_t oi = 0;
    size_t i = 0;
    while (i < len && oi < cap) {
        uint32_t lo = seqs[i++];
        uint32_t hi = lo;
        while (i < len && seqs[i] == hi + 1) hi = seqs[i++];
        mins[oi] = lo;
        maxs[oi] = hi;
        oi++;
    }
    return oi;
}

size_t mcp_rak_session_drain_ack_queue(mcp_rak_session_t* s,
                                        uint32_t* mins, uint32_t* maxs,
                                        size_t cap) {
    if (!s || !mins || !maxs || !s->ack_queue_len) return 0;
    size_t n = collapse_ranges(s->ack_queue, s->ack_queue_len, mins, maxs, cap);
    s->ack_queue_len = 0;
    return n;
}

size_t mcp_rak_session_drain_nack_queue(mcp_rak_session_t* s,
                                         uint32_t* mins, uint32_t* maxs,
                                         size_t cap) {
    if (!s || !mins || !maxs || !s->nack_queue_len) return 0;
    size_t n = collapse_ranges(s->nack_queue, s->nack_queue_len, mins, maxs, cap);
    s->nack_queue_len = 0;
    return n;
}

// --------------------------------------------------------------------------
// Build an encapsulated DataPacket_4 (0x84) datagram
// Format (per RakLib PHP):
//   [0x84] [3-byte LE seqNo] { encapsulated records... }
// Each encapsulated record:
//   [flags byte] [16-bit BE bit-length] [messageIndex 3-byte LE if RELIABLE]
//   [orderIndex 3-byte LE + orderChannel 1-byte if ORDERED]
//   [payload bytes]
// --------------------------------------------------------------------------
#define ENCAP_FLAG_RELIABILITY(r) (((r) & 0x07) << 5)
#define ENCAP_FLAG_HAS_SPLIT      0x10

size_t mcp_rak_session_build_datagram(mcp_rak_session_t* s,
                                       uint8_t* out, size_t out_cap,
                                       const uint8_t* payload, size_t plen,
                                       uint8_t reliability, uint8_t order_channel) {
    if (!s || !out || !payload || plen == 0) return 0;
    uint32_t seq = s->send_seq++;

    size_t i = 0;
    // DataPacket_4 header: type byte + 3-byte LE sequence number
    if (i + 4 > out_cap) return 0;
    out[i++] = 0x84;
    out[i++] = (uint8_t)(seq & 0xFF);
    out[i++] = (uint8_t)((seq >> 8) & 0xFF);
    out[i++] = (uint8_t)((seq >> 16) & 0xFF);

    // Encapsulated record header
    bool is_reliable  = (reliability == MCP_RELIABILITY_RELIABLE ||
                         reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
                         reliability == MCP_RELIABILITY_RELIABLE_SEQUENCED);
    bool is_ordered   = (reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
                         reliability == MCP_RELIABILITY_UNRELIABLE_SEQUENCED);
    uint8_t flags = ENCAP_FLAG_RELIABILITY(reliability);
    if (i + 3 > out_cap) return 0;
    out[i++] = flags;
    // 16-bit bit-length of payload (big-endian)
    uint16_t bit_len = (uint16_t)(plen * 8);
    out[i++] = (uint8_t)(bit_len >> 8);
    out[i++] = (uint8_t)(bit_len & 0xFF);

    // messageIndex (3-byte LE) for RELIABLE
    if (is_reliable) {
        if (i + 3 > out_cap) return 0;
        uint32_t mi = s->message_index++;
        out[i++] = (uint8_t)(mi & 0xFF);
        out[i++] = (uint8_t)((mi >> 8) & 0xFF);
        out[i++] = (uint8_t)((mi >> 16) & 0xFF);
    }
    // orderIndex (3-byte LE) + orderChannel (1 byte) for ORDERED
    if (is_ordered) {
        if (i + 4 > out_cap) return 0;
        uint32_t oi_idx = s->channel_index[(uint8_t)(order_channel & 0x1F)]++;
        out[i++] = (uint8_t)(oi_idx & 0xFF);
        out[i++] = (uint8_t)((oi_idx >> 8) & 0xFF);
        out[i++] = (uint8_t)((oi_idx >> 16) & 0xFF);
        out[i++] = order_channel & 0x1F;
    }

    // Payload
    if (i + plen > out_cap) return 0;
    memcpy(out + i, payload, plen);
    i += plen;

    // Store in recovery queue
    uint32_t slot = seq % MCP_RECOVERY_CAP;
    if (s->recovery[slot].data) free(s->recovery[slot].data);
    s->recovery[slot].data = (uint8_t*)malloc(i);
    if (s->recovery[slot].data) {
        memcpy(s->recovery[slot].data, out, i);
        s->recovery[slot].len = i;
        s->recovery[slot].seq = seq;
        s->recovery[slot].send_time_ms = mcp_utils_time_ms();
    }
    return i;
}

// --------------------------------------------------------------------------
// Enqueue for deferred send
// --------------------------------------------------------------------------
bool mcp_rak_session_enqueue(mcp_rak_session_t* s,
                              const uint8_t* data, size_t len) {
    if (!s || !data || len == 0) return false;
    if (s->send_queue_count == s->send_queue_cap) {
        size_t nc = s->send_queue_cap == 0 ? 8 : s->send_queue_cap * 2;
        uint8_t** nb = (uint8_t**)realloc(s->send_queue_bufs, nc * sizeof(uint8_t*));
        size_t*   nl = (size_t*)  realloc(s->send_queue_lens, nc * sizeof(size_t));
        if (!nb || !nl) return false;
        s->send_queue_bufs = nb;
        s->send_queue_lens = nl;
        s->send_queue_cap  = nc;
    }
    uint8_t* copy = (uint8_t*)malloc(len);
    if (!copy) return false;
    memcpy(copy, data, len);
    s->send_queue_bufs[s->send_queue_count] = copy;
    s->send_queue_lens[s->send_queue_count] = len;
    s->send_queue_count++;
    return true;
}

// --------------------------------------------------------------------------
// Timeout
// --------------------------------------------------------------------------
bool mcp_rak_session_is_timed_out(const mcp_rak_session_t* s,
                                    uint64_t now_ms, uint64_t timeout_ms) {
    if (!s) return true;
    return (now_ms - s->last_activity_ms) > timeout_ms;
}
