// SPDX-License-Identifier: LGPL-3.0-or-later
// Session.c — per-client RakNet state machine, Protocol 84 / MCPE 0.15.10
// Ported from raklib/server/Session.php (PocketMine-MP 4.0.0 legacy branch)
#include "Session.h"
#include "SessionManager.h"
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
    s->last_packet_received_ms      = s->last_activity_ms;
    s->last_game_packet_received_ms = s->last_activity_ms;
    s->last_ping_received_ms        = 0;
    s->last_packet_sent_ms          = s->last_activity_ms;
    s->start_time_ms    = s->last_activity_ms;
    s->is_active    = false;
    s->is_disconnecting = false;  // Bug 2 fix
    for (int i = 0; i < 32; ++i) s->channel_index[i] = 0;
    for (int i = 0; i < 32; ++i) {
        s->recv_order_index[i] = 0;
        s->recv_order_advanced_ms[i] = s->last_activity_ms;
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            s->order_hold[i][k].used = false;
            s->order_hold[i][k].data = NULL;
        }
    }
    // FIX #1: no single split anymore — every slot starts unused.
    for (int i = 0; i < MCP_MAX_CONCURRENT_SPLITS; ++i) {
        s->split_slots[i].in_use = false;
    }
    // FIX #2: reliable message-index receive window.
    s->reliable_window_start = 0;
    s->last_reliable_index   = -1;
    memset(s->reliable_recv_window, 0, sizeof(s->reliable_recv_window));
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
    // Free split store segments — FIX #1: iterate every concurrent slot.
    for (int si = 0; si < MCP_MAX_CONCURRENT_SPLITS; ++si) {
        if (s->split_slots[si].in_use) {
            for (uint32_t i = 0; i < s->split_slots[si].count && i < MCP_MAX_SPLIT_SEGMENTS; ++i) {
                free(s->split_slots[si].segments[i]);
            }
        }
    }
    // FIX: free any packets still waiting in the ordering hold buffers —
    // otherwise every held-back packet at the moment a session is torn
    // down leaks.
    for (int c = 0; c < 32; ++c) {
        for (int k = 0; k < MCP_ORDER_HOLD_CAP; ++k) {
            if (s->order_hold[c][k].used && s->order_hold[c][k].data) {
                free(s->order_hold[c][k].data);
            }
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

// Ascending-order comparator for uint32_t, used by qsort() below.
static int cmp_u32(const void* a, const void* b) {
    uint32_t ua = *(const uint32_t*)a, ub = *(const uint32_t*)b;
    return (ua > ub) - (ua < ub);
}

// Collapse an array of seqs into (min,max) ranges.
// FIX (audit priority 5): this used to assume the input array already
// arrived in ascending order — true only as long as the underlying
// network delivers ACKed/NACKed datagrams in order. Real UDP reordering
// (e.g. seqs queued in the order 5,3,4,8,7) broke that assumption
// silently: the old code would emit 4 disjoint ranges ([5,5],[3,4],[8,8],
// [7,7]) instead of the 2 truly-contiguous ones ([3,5],[7,8]). No
// sequence numbers were lost by that — every one still ended up in some
// range — but every extra range costs bytes in the ACK/NACK datagram
// sent back to the client, and that overhead scales with how reordered
// the network is at the time, which is exactly when the connection can
// least afford wasted bandwidth. Sorting first guarantees the minimal,
// correct set of ranges regardless of arrival order.
static size_t collapse_ranges(const uint32_t* seqs_in, size_t len,
                               uint32_t* mins, uint32_t* maxs, size_t cap) {
    if (!len) return 0;
    // Work on a local sorted copy — callers pass s->ack_queue /
    // s->nack_queue directly, which must stay untouched until the
    // caller resets its length itself.
    uint32_t sorted[MCP_MAX_ACK_QUEUE > MCP_MAX_NACK_QUEUE ? MCP_MAX_ACK_QUEUE : MCP_MAX_NACK_QUEUE];
    size_t n = len < (sizeof(sorted) / sizeof(sorted[0])) ? len : (sizeof(sorted) / sizeof(sorted[0]));
    memcpy(sorted, seqs_in, n * sizeof(uint32_t));
    qsort(sorted, n, sizeof(uint32_t), cmp_u32);

    size_t oi = 0;
    size_t i = 0;
    while (i < n && oi < cap) {
        uint32_t lo = sorted[i++];
        uint32_t hi = lo;
        // Also skip exact duplicates (queue_ack/queue_nack already dedup
        // on insert, but this stays correct even if that ever changes).
        while (i < n && (sorted[i] == hi + 1 || sorted[i] == hi)) hi = sorted[i++];
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
// Build one encapsulated DataPacket_4 (0x84) datagram — one fragment.
// Format (per RakLib PHP):
//   [0x84] [3-byte LE seqNo] { encapsulated record }
// Encapsulated record:
//   [flags byte] [16-bit BE bit-length]
//   [messageIndex 3-byte LE]                               — if RELIABLE
//   [orderIndex 3-byte LE + orderChannel 1-byte]           — if ORDERED
//   [splitID 2-byte BE] [splitIdx 4-byte BE] [splitCount 4-byte BE]  — if has_split
//   [payload slice bytes]
//
// INTERNAL — for payloads that may exceed MTU use mcp_rak_session_send_payload.
// --------------------------------------------------------------------------
#define ENCAP_FLAG_RELIABILITY(r) (((r) & 0x07) << 5)
#define ENCAP_FLAG_HAS_SPLIT      0x10

size_t mcp_rak_session_build_datagram(mcp_rak_session_t* s,
                                       uint8_t* out, size_t out_cap,
                                       const uint8_t* payload, size_t plen,
                                       uint8_t reliability, uint8_t order_channel,
                                       bool has_split, uint16_t split_id,
                                       uint32_t split_index, uint32_t split_count) {
    if (!s || !out || !payload || plen == 0) return 0;
    uint32_t seq = s->send_seq++;

    size_t i = 0;

    // DataPacket_4 header: type byte (0x84) + 3-byte LE sequence number
    if (i + 4 > out_cap) return 0;
    out[i++] = 0x84;
    out[i++] = (uint8_t)(seq & 0xFF);
    out[i++] = (uint8_t)((seq >> 8) & 0xFF);
    out[i++] = (uint8_t)((seq >> 16) & 0xFF);

    bool is_reliable = (reliability == MCP_RELIABILITY_RELIABLE ||
                        reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
                        reliability == MCP_RELIABILITY_RELIABLE_SEQUENCED ||
                        reliability == MCP_RELIABILITY_RELIABLE_WITH_ACK_RECEIPT ||
                        reliability == MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT);
    // FIX (priority 9 / RakNet compatibility): UNRELIABLE_SEQUENCED does
    // NOT carry an orderIndex/orderChannel — it carries a sequencingIndex
    // instead (see RakNet's InternalPacket: order fields are only present
    // for the *_ORDERED reliabilities). This used to lump
    // UNRELIABLE_SEQUENCED in with RELIABLE_ORDERED here, which — had
    // anything in this codebase actually sent UNRELIABLE_SEQUENCED — would
    // have written the wrong field entirely and desynced the two sides'
    // interpretation of the datagram. Nothing currently sends that
    // reliability value (grep confirms), so this couldn't have caused the
    // reported disconnects, but it's a real correctness bug the moment
    // anything does start using it (e.g. entity position updates, a
    // natural fit for "only the latest matters, ordering channel not
    // needed").
    bool is_ordered  = (reliability == MCP_RELIABILITY_RELIABLE_ORDERED ||
                        reliability == MCP_RELIABILITY_RELIABLE_ORDERED_WITH_ACK_RECEIPT);

    // Flags: reliability bits (5-7) | split flag (bit 4)
    uint8_t flags = ENCAP_FLAG_RELIABILITY(reliability);
    if (has_split) flags |= ENCAP_FLAG_HAS_SPLIT;

    // 16-bit bit-length of this payload slice (big-endian)
    uint16_t bit_len = (uint16_t)(plen * 8);
    if (i + 3 > out_cap) return 0;
    out[i++] = flags;
    out[i++] = (uint8_t)(bit_len >> 8);
    out[i++] = (uint8_t)(bit_len & 0xFF);

    // messageIndex (3-byte LE) — one counter per RELIABLE datagram
    if (is_reliable) {
        if (i + 3 > out_cap) return 0;
        uint32_t mi = s->message_index++;
        out[i++] = (uint8_t)(mi & 0xFF);
        out[i++] = (uint8_t)((mi >> 8) & 0xFF);
        out[i++] = (uint8_t)((mi >> 16) & 0xFF);
    }

    // orderIndex (3-byte LE) + orderChannel (1 byte)
    if (is_ordered) {
        if (i + 4 > out_cap) return 0;
        uint32_t oi_idx = s->channel_index[(uint8_t)(order_channel & 0x1F)]++;
        out[i++] = (uint8_t)(oi_idx & 0xFF);
        out[i++] = (uint8_t)((oi_idx >> 8) & 0xFF);
        out[i++] = (uint8_t)((oi_idx >> 16) & 0xFF);
        out[i++] = order_channel & 0x1F;
    }

    // Split packet header — only when has_split=true.
    //
    // BUG FIX: wire order for RakNet's split info is FIXED at
    // splitCount(4) + splitID(2) + splitIndex(4) — this matches the
    // real RakNet/RakLib spec (see raklib\protocol\EncapsulatedPacket
    // ::SPLIT_INFO_LENGTH = 4+2+4 in PocketMine-MP's RakLib, and
    // RakNet's own InternalPacket.h field order: splitPacketId,
    // splitPacketIndex, splitPacketCount are tracked separately but
    // serialized on the wire as count-first). This function used to
    // write splitID(2) + splitIndex(4) + splitCount(4) instead — a
    // different order with the same total size (10 bytes), so it
    // never crashed or got rejected, but every field the real MCPE
    // client read out of a split fragment was wrong (it read our
    // splitID as half of splitCount, etc). That corrupts the client's
    // split-packet reassembly for every packet the server has to
    // fragment (large chunk sends, big inventory syncs, ...) — and
    // with any latency/reordering, more fragments end up in flight at
    // once, making the corruption worse until the client's RakNet
    // layer gives up and disconnects. This was the read side
    // (parse_encap_record in SessionManager.c) all along — that one
    // already used the correct count/id/index order — so only the
    // write side needed fixing to match it.
    if (has_split) {
        if (i + 10 > out_cap) return 0;
        // splitCount (4-byte BE)
        out[i++] = (uint8_t)(split_count >> 24);
        out[i++] = (uint8_t)(split_count >> 16);
        out[i++] = (uint8_t)(split_count >>  8);
        out[i++] = (uint8_t)(split_count & 0xFF);
        // splitID (2-byte BE)
        out[i++] = (uint8_t)(split_id >> 8);
        out[i++] = (uint8_t)(split_id & 0xFF);
        // splitIndex (4-byte BE)
        out[i++] = (uint8_t)(split_index >> 24);
        out[i++] = (uint8_t)(split_index >> 16);
        out[i++] = (uint8_t)(split_index >>  8);
        out[i++] = (uint8_t)(split_index & 0xFF);
    }

    // Payload slice
    if (i + plen > out_cap) return 0;
    memcpy(out + i, payload, plen);
    i += plen;

    // Store in recovery queue for ACK-based retransmit.
    // FIX (audit priority 6): only RELIABLE* datagrams belong here.
    // UNRELIABLE (and UNRELIABLE_SEQUENCED) are — by RakNet's own
    // contract — never acknowledged by the remote side, so a slot
    // storing one would NEVER be freed by handle_ack(): it would sit
    // occupied until another seq's modulo happened to reuse the same
    // slot (wasting capacity that RELIABLE traffic could have used
    // instead — exactly the recovery-exhaustion risk this audit is
    // tracking), AND retransmit_stale() would dutifully keep resending
    // that same "unreliable" datagram every 5 seconds forever, since
    // nothing would ever remove it. Gating this on is_reliable fixes
    // both: unreliable sends are fire-and-forget, as intended, and
    // recovery capacity is spent only on datagrams that can actually
    // be confirmed and might actually need a resend.
    if (is_reliable) {
        uint32_t slot = (seq & MCP_SEQ24_MASK) % MCP_RECOVERY_CAP;
        if (s->recovery[slot].data) {
            // FIX: this slot still holds an un-ACKed datagram — we're about
            // to overwrite (permanently lose) it before the client ever
            // confirmed receipt. This used to happen silently; now it's
            // counted so it can be correlated with disconnects instead of
            // vanishing without a trace (see the disconnect log in
            // SessionManager.c, point #13 of the audit).
            s->diag_recovery_overwrites++;
            free(s->recovery[slot].data);
            // occupancy unchanged: freeing an old occupant and storing a
            // new one in the same slot is a like-for-like replacement,
            // not growth
        } else {
            s->diag_recovery_occupied++;
            if (s->diag_recovery_occupied > s->diag_recovery_peak)
                s->diag_recovery_peak = s->diag_recovery_occupied;
        }
        s->recovery[slot].data = (uint8_t*)malloc(i);
        if (s->recovery[slot].data) {
            memcpy(s->recovery[slot].data, out, i);
            s->recovery[slot].len          = i;
            s->recovery[slot].seq          = seq & MCP_SEQ24_MASK;
            s->recovery[slot].send_time_ms = mcp_utils_time_ms();
        }
    }
    return i;
}

// --------------------------------------------------------------------------
// mcp_rak_session_send_payload — automatic MTU splitting, then UDP send.
//
// Single datagram path (plen <= mtu-14): sends one DataPacket_4, has_split=false.
// Split path (plen > mtu-14): divides into (mtu-24)-byte fragments, sends each
//   as a separate DataPacket_4 with has_split=true, same splitID. The MCPE
//   client reassembles using splitIndex/splitCount before processing.
//
// Mirrors PMMP raklib/server/Session.php::addEncapsulatedToQueue().
// --------------------------------------------------------------------------
bool mcp_rak_session_send_payload(mcp_rak_session_t* s,
                                   mcp_rak_session_manager_t* sm,
                                   const uint8_t* payload, size_t plen,
                                   uint8_t reliability, uint8_t order_channel) {
    if (!s || !sm || !payload || plen == 0) return false;

    // s->mtu is the max IP PACKET size, so the UDP payload budget has to
    // give up one IPv4 header and one UDP header first. See the long note
    // on MCP_IPV4_HEADER_BYTES in Session.h — without this every fragment
    // went out 16 bytes past the 1500-byte MTU and Linux dropped it.
    if ((size_t)(s->mtu) <= (size_t)(MCP_IPV4_HEADER_BYTES + MCP_UDP_HEADER_BYTES))
        return false;
    size_t udp_budget = (size_t)(s->mtu)
                        - MCP_IPV4_HEADER_BYTES
                        - MCP_UDP_HEADER_BYTES;

    // Max payload in a single datagram (no split header):
    // udp_budget - datagram_header(4) - encap_base(10)
    size_t max_single = udp_budget
                        - MCP_DATAGRAM_HEADER_BYTES
                        - MCP_ENCAP_BASE_BYTES;
    if (max_single == 0) return false;

    if (plen <= max_single) {
        // Fast path — no splitting.
        uint8_t buf[RAKLIB_MAX_MTU + 64];
        size_t n = mcp_rak_session_build_datagram(
                       s, buf, sizeof(buf), payload, plen,
                       reliability, order_channel,
                       false, 0, 0, 0);
        if (n == 0) return false;
        return mcp_rak_session_manager_send(sm, buf, n, s->ip, s->port) > 0;
    }

    // Split path.
    // Per-fragment payload (with split header): udp_budget - 4 - 10 - 10
    size_t frag_size = udp_budget
                       - MCP_DATAGRAM_HEADER_BYTES
                       - MCP_ENCAP_BASE_BYTES
                       - MCP_ENCAP_SPLIT_BYTES;
    if (frag_size == 0) return false;

    uint32_t split_count = (uint32_t)((plen + frag_size - 1) / frag_size);
    if (split_count > MCP_MAX_SPLIT_SEGMENTS) return false;

    uint16_t split_id = s->split_id_counter++;
    uint8_t  buf[RAKLIB_MAX_MTU + 64];
    bool     ok = true;

    // FIX (chunks not rendering / stuck in the sky, Linux release only):
    // per PMMP SendReliabilityLayer::sendPacket(), each FRAGMENT of a split
    // packet is its own reliable message on the wire: messageIndex is
    // incremented once per fragment, while all fragments of one split share
    // the packet's single orderIndex. The previous "fix" here did the exact
    // opposite — it pinned messageIndex to the same value for every fragment
    // (so the client's RakNet layer saw fragments 2..N as duplicates of the
    // first one and dropped them; the split never completed reassembly and
    // the client rendered nothing — visible as an empty world with the
    // player falling in the sky — while chat, which is small enough to avoid
    // splitting, kept working) and it incremented orderIndex per fragment
    // (irrelevant for RELIABLE-unordered chunk sends, but wrong for any
    // future RELIABLE_ORDERED split).
    //
    // Correct behavior:
    //   - messageIndex: leave s->message_index alone — build_datagram()
    //     increments it naturally once per fragment.
    //   - orderIndex: snapshot once here and restore before each fragment so
    //     all fragments carry the same orderIndex (build_datagram increments
    //     the per-channel counter only when the reliability is ordered; for
    //     unordered chunk sends this value never reaches the wire).
    uint32_t saved_channel_index = s->channel_index[(uint8_t)(order_channel & 0x1F)];

    for (uint32_t idx = 0; idx < split_count; ++idx) {
        size_t offset   = (size_t)idx * frag_size;
        size_t this_len = (plen - offset < frag_size) ? (plen - offset) : frag_size;

        // Same orderIndex for every fragment of this split packet.
        s->channel_index[(uint8_t)(order_channel & 0x1F)] = saved_channel_index;

        size_t n = mcp_rak_session_build_datagram(
                       s, buf, sizeof(buf),
                       payload + offset, this_len,
                       reliability, order_channel,
                       true, split_id, idx, split_count);
        if (n == 0) { ok = false; continue; }
        if (mcp_rak_session_manager_send(sm, buf, n, s->ip, s->port) <= 0)
            ok = false;
    }

    // After the loop:
    //   - message_index was advanced once per fragment by build_datagram()
    //     (exactly what PMMP's SendReliabilityLayer does for split packets).
    //   - the per-channel order counter was restored to its snapshot before
    //     each fragment, so it ends at snapshot+1: all fragments shared one
    //     orderIndex and the next packet gets the following one.
    return ok;
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
// FIX (AFK kick): a session is only stale when NO datagram whatsoever has
// arrived for the timeout window. Keepalive PINGs (0x00) and ACK/NACK
// traffic both count as activity — that is exactly how RakLib
// (Connection::lastUpdate) and PMMP define the session timeout. Under the
// old rule ("only non-PING game packets refresh the clock"), an idle but
// perfectly healthy player — one who left the game running without input —
// was force-disconnected after 30s with "timeout" even though the
// connection was fully alive.
bool mcp_rak_session_is_timed_out(const mcp_rak_session_t* s,
                                    uint64_t now_ms, uint64_t timeout_ms) {
    if (!s) return true;
    // FIX: guard against unsigned underflow. If last_activity_ms is ever
    // ahead of now_ms (clock adjustment, or a timestamp written from a
    // slightly stale `now` on another call path), the unsigned subtraction
    // would wrap around to a huge value and report an immediate false
    // timeout for an otherwise perfectly active session.
    if (s->last_activity_ms >= now_ms) return false;
    return (now_ms - s->last_activity_ms) > timeout_ms;
}
