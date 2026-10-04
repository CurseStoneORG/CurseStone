// SPDX-License-Identifier: LGPL-3.0-or-later
// Session.h — per-client RakNet state machine. Pure C.
// Compatible with MCPE 0.15.10 / protocol 84 / RakLib 0.8.x
#pragma once

#include "UDPServerSocket.h"
#include "protocol/PacketReliability.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Forward declaration — full definition in SessionManager.h
typedef struct mcp_rak_session_manager mcp_rak_session_manager_t;

// ---- Session states (mirrors PMMP Session.php) ----
#define MCP_SESSION_STATE_UNCONNECTED  0
#define MCP_SESSION_STATE_CONNECTING_1 1   // after OCR1 → waiting OCR2
#define MCP_SESSION_STATE_CONNECTING_2 2   // after OCR2 → waiting CLIENT_CONNECT (0x09)
#define MCP_SESSION_STATE_CONNECTED    3   // fully established

// Maximum encapsulated packet size inside one DataPacket datagram (per PMMP).
#define MCP_SESSION_MAX_SPLIT_SIZE   128
#define MCP_SESSION_MAX_SPLIT_COUNT    4
#define MCP_SESSION_WINDOW_SIZE     2048

// Connection timeout for the session sweep, in milliseconds (30s — the
// RakLib/PMMP default). The timeout clock means "no datagram received at
// all": keepalive PINGs and ACK/NACK traffic both count as activity, so
// a player who stays idle (AFK) with the game open is never kicked; only
// a genuinely dead connection (app closed, network lost) is reaped.
#define MCP_SESSION_TIMEOUT_MS      30000

// FIX (audit priority 4/12): RakNet's 24-bit index spaces (datagram
// sequence number, reliable message index, ordering index) all wrap at
// this boundary. Shared here (not just in SessionManager.c, where the
// wrap-safe comparison helpers live) because Session.c's send path also
// needs it: send_seq is kept as a plain uint32_t counter that grows
// without ever wrapping in memory (simpler than wrapping the counter
// itself), while only the 3 low bytes are ever written to the wire —
// masking with this here keeps what's stored in the recovery queue
// (recovery[slot].seq) consistent with the 24-bit values that
// subsequently arrive back in ACK/NACK from the client, which are
// decoded straight off the wire and can never be anything but 24-bit.
// Without this mask, a session sending enough traffic to grow send_seq
// past 0xFFFFFF would find that recovery[slot].seq (still the full,
// un-masked 32-bit counter value) could never again match an incoming
// ACK's seq value (always 24-bit) for any datagram sent after that
// point — retransmission bookkeeping would silently stop being able to
// recognize its own acknowledgments.
#define MCP_SEQ24_MASK   0x00FFFFFFu

// RakNet encapsulated packet: the header inside a Data datagram.
// Simplified: only tracks fields needed for Proto 84 / MCPE 0.15.10.
typedef struct mcp_encapsulated_header {
    uint8_t  reliability;       // PacketReliability value
    bool     has_split;
    uint32_t message_index;     // present if RELIABLE
    uint32_t order_index;       // present if ORDERED
    uint8_t  order_channel;
    uint16_t split_id;
    uint32_t split_index;
    uint32_t split_count;
} mcp_encap_header_t;

// A segment of the split-packet reassembly buffer.
#define MCP_MAX_SPLIT_SEGMENTS 256
// FIX #1: was a single mcp_split_store_t (one split in flight at a time).
// Chunk streaming can fragment several distinct packets whose fragments
// interleave in flight; a single slot forced the server to discard
// whatever it had reassembled so far of split A the instant a fragment
// of split B arrived. MCP_MAX_CONCURRENT_SPLITS gives each split_id its
// own slot, keyed by split_id, so unrelated fragmented packets never
// collide with each other.
#define MCP_MAX_CONCURRENT_SPLITS 32
typedef struct mcp_split_store {
    uint16_t split_id;
    uint32_t count;
    uint32_t received;
    uint8_t* segments[MCP_MAX_SPLIT_SEGMENTS]; // heap-allocated
    size_t   seg_sizes[MCP_MAX_SPLIT_SEGMENTS];
    bool     in_use;
    // FIX: ordering info from the first fragment, applied once to the
    // reassembled packet (see deliver_ordered() in SessionManager.c).
    bool     is_ordered;
    uint32_t order_index;
    uint8_t  order_channel;
    // FIX (critical, confirmed by server.log): reliability + message_index
    // from the first fragment. RakNet's real reference implementation
    // assigns a single reliableMessageNumber to a fragmented message
    // BEFORE splitting it — every physical fragment shares that same
    // index. That means the reliable-index duplicate/staleness check
    // (reliable_index_accept in SessionManager.c) must run exactly once,
    // against the REASSEMBLED message — never per-fragment, which would
    // reject every fragment after the first as "already seen" the moment
    // the first one set that index's bit in the receive window.
    bool     is_reliable;
    uint32_t message_index;
    // FIX: if a fragment never arrives, this store used to sit "in_use"
    // forever — not a disconnect by itself, but unbounded per-session
    // memory that never gets reclaimed for a session that keeps sending
    // new (still-incomplete) split packets. started_ms lets the tick
    // sweep evict a stale incomplete store after a timeout.
    uint64_t started_ms;
    // FIX: last time a fragment for THIS split_id was stored — used to
    // pick the least-recently-touched slot for eviction when all
    // MCP_MAX_CONCURRENT_SPLITS slots are occupied by different split_ids.
    uint64_t last_fragment_ms;
} mcp_split_store_t;

#define MCP_MAX_ACK_QUEUE    256
#define MCP_MAX_NACK_QUEUE   256
// FIX: raised from 512 → 4096. With chunk bursts of 60+ reliable
// fragments per tick (documented elsewhere in this codebase), 512
// in-flight slots could be exhausted before the corresponding ACKs
// came back, silently overwriting (and thus permanently losing) a
// reliable datagram that was never acknowledged. If that lost
// datagram carried a RELIABLE_ORDERED fragment (e.g. part of the
// login/spawn sequence on channel 0), the client's own ordering
// buffer would stall waiting for an index that would never arrive.
// 4096 slots (~32KB of pointers/metadata per session, negligible)
// gives enough headroom for several ticks worth of chunk bursts
// even under real-world latency before wraparound can occur.
#define MCP_RECOVERY_CAP     4096

// Per-session send queue slot.
typedef struct mcp_send_slot {
    uint8_t* data;
    size_t   len;
    uint64_t send_time_ms;
    uint32_t seq;
} mcp_send_slot_t;

typedef struct mcp_rak_session {
    // Identity
    uint8_t   ip[4];
    uint16_t  port;
    int64_t   client_id;
    int64_t   server_id;

    // State machine
    uint8_t   state;            // MCP_SESSION_STATE_*

    // Sequence numbers
    uint32_t  send_seq;
    int32_t   last_recv_seq;    // -1 = nothing received yet

    // Sliding window
    uint32_t  window_start;
    uint32_t  window_end;
    bool      recv_window[MCP_SESSION_WINDOW_SIZE];

    // ACK / NACK queues (sequence numbers pending transmission)
    uint32_t  ack_queue[MCP_MAX_ACK_QUEUE];
    size_t    ack_queue_len;
    uint32_t  nack_queue[MCP_MAX_NACK_QUEUE];
    size_t    nack_queue_len;

    // Send queue (DataPacket 0x84 datagrams awaiting flush)
    uint8_t** send_queue_bufs;
    size_t*   send_queue_lens;
    size_t    send_queue_count;
    size_t    send_queue_cap;

    // Recovery queue (for retransmit): indexed by seq % MCP_RECOVERY_CAP
    mcp_send_slot_t recovery[MCP_RECOVERY_CAP];

    // Reliability counters
    uint32_t  message_index;    // monotonically increasing per RELIABLE pkt
    uint32_t  channel_index[32];// per ordered-channel index (SEND side)

    // FIX: RELIABLE_ORDERED receive-side reordering. RakNet guarantees
    // packets on an ordered channel are delivered to the application in
    // the order they were SENT, even if they arrive out of order over
    // UDP. Before this fix, the server decoded order_index/order_channel
    // off the wire and then discarded them — every packet was handed to
    // the game layer in physical arrival order instead. That's invisible
    // as long as the network delivers packets in order, but ordinary UDP
    // jitter (no packet loss required, just reordering) breaks it, and
    // MCPE clients that send anything on an ordered channel would have
    // it processed out of sequence server-side.
    uint32_t  recv_order_index[32];  // next index expected, per channel
    // FIX: last time recv_order_index[chan] actually advanced. Needed to
    // detect a channel stalled on a gap where the missing packet AND
    // every one of its retransmits were lost — expire_order_holds() only
    // ever frees packets that arrived out of order and are WAITING; it
    // has no way to notice a gap with nothing behind it at all. Without
    // this, that gap would never close and the channel would be stuck
    // forever (every later ordered packet held indefinitely, itself
    // never expiring because deliver_ordered() only holds packets that
    // arrived — a hole with nothing behind it isn't "held" by anything).
    uint64_t  recv_order_advanced_ms[32];
    // Packets that arrived ahead of the expected index, held until the
    // gap is filled. Small ring buffer per channel is enough — MCPE's
    // own traffic patterns never realistically get more than a handful
    // of ordered packets in flight out of order at once.
#define MCP_ORDER_HOLD_CAP 32
    struct {
        bool     used;
        uint32_t order_index;
        uint8_t* data;
        size_t   len;
        // FIX #3-C: when this slot was populated, so a stale held packet
        // (the gap it's waiting on will never be filled — e.g. the
        // missing packet's own retransmit was also lost) can be evicted
        // instead of occupying the slot forever.
        uint64_t stored_ms;
    } order_hold[32][MCP_ORDER_HOLD_CAP];

    // Split packet reassembly — FIX #1: multiple concurrent splits,
    // keyed by split_id, instead of one shared slot.
    mcp_split_store_t split_slots[MCP_MAX_CONCURRENT_SPLITS];

    // FIX #2: reliable message-index receive window. The datagram
    // sequence-number window above (recv_window) tracks RakNet transport
    // datagrams; it is NOT the same thing as the reliable message index
    // carried inside RELIABLE* encapsulated records, which identifies
    // fiable *messages* independent of which datagram carried them (a
    // retransmit reuses a new sequence number for the same message
    // index). Without tracking this separately, a retransmitted reliable
    // message can't be told apart from a genuinely new one, and reordered
    // reliable messages have no duplicate/staleness check of their own.
#define MCP_RELIABLE_WINDOW_SIZE 2048
    uint32_t  reliable_window_start;
    bool      reliable_recv_window[MCP_RELIABLE_WINDOW_SIZE];
    int32_t   last_reliable_index; // -1 = nothing received yet

    // Payload-aware duplicate detection for reliable messages.
    //
    // reliable_recv_window[] above can only answer "have I seen this
    // message index?". That is enough to suppress a genuine RETRANSMIT
    // (which reuses both the index and the bytes), but it cannot tell a
    // retransmit apart from a client that REUSES an index for a different
    // message. Some clients in the wild (e.g. the LegacyBot stress
    // tester) never increment reliableMessageNumber and send 0 for every
    // reliable-ordered packet, so the window accepted the first packet
    // and silently discarded the rest as "duplicates" — the
    // CLIENT_HANDSHAKE and the LoginPacket never reached the game layer,
    // no session was ever opened, and the log stayed completely empty.
    // This table remembers the (index, payload-hash) of recently accepted
    // messages so a repeat is only dropped when the bytes match too.
    //
    // It is DIRECT-MAPPED by `message_index % MCP_RELIABLE_WINDOW_SIZE`
    // and MUST cover exactly the same index space as reliable_recv_window
    // above. It used to be a 256-slot ring while the window was 2048:
    // a retransmit older than 256 messages but still inside the window
    // found its window bit set yet its payload long since evicted, so
    // reliable_already_delivered() returned false and the message was
    // DELIVERED TO THE GAME LAYER A SECOND TIME. That needs real packet
    // loss to trigger — i.e. it is invisible on a lossless LAN/loopback
    // and only shows up on a hosted server over the Internet, exactly
    // where duplicated actions/state hurt most. With one entry per window
    // slot the lookup is also O(1) instead of a 256-entry scan per
    // message.
    struct {
        uint32_t index;
        uint32_t hash;
        uint32_t len;
        bool     used;
    } reliable_seen[MCP_RELIABLE_WINDOW_SIZE];

    // Same idea for RELIABLE_ORDERED channels. A stale order_index is
    // normally a retransmit of something already delivered and is dropped;
    // but a client that keeps reusing one order_index for every packet
    // would otherwise have ALL of its traffic after the first packet
    // discarded silently, stranding the session before login.
#define MCP_ORDER_DELIVERED_SLOTS 64
    struct {
        uint8_t  chan;
        uint32_t index;
        uint32_t hash;
        uint32_t len;
        bool     used;
    } order_delivered[MCP_ORDER_DELIVERED_SLOTS];
    uint32_t  order_delivered_next;
    bool      diag_order_index_reuse_logged;

    // ---- Diagnostics (point #12/#13 in the audit): counters surfaced in
    // the disconnect log so a timeout/kick can be correlated with what
    // was actually happening in the reliability layer at the time. ----
    uint64_t  diag_recv_packets;
    uint64_t  diag_sent_packets;
    // FIX: one-shot flag so a hard UDP sendto() failure (e.g. outbound UDP
    // blocked by a hosting panel's firewall) is logged exactly once per
    // session instead of spamming the console on every retransmit.
    bool      diag_send_fail_logged;
    uint64_t  diag_ack_received;
    uint64_t  diag_nack_received;
    uint64_t  diag_retransmits;
    uint32_t  diag_recovery_occupied; // current count — kept incrementally,
                                       // never scanned, so it's cheap to
                                       // update on every send/ack
    uint32_t  diag_recovery_peak;
    uint32_t  diag_recovery_overwrites;
    uint32_t  diag_split_active;
    uint32_t  diag_split_completed;
    uint32_t  diag_split_expired;
    uint32_t  diag_split_evicted;
    uint32_t  diag_order_hold_peak;
    uint32_t  diag_order_hold_full;
    uint32_t  diag_order_duplicate;
    // FIX: separate counter for the forced-skip case above — a channel
    // that was stuck long enough to have its gap declared permanently
    // lost and skipped. Kept distinct from diag_order_duplicate (an
    // unrelated event: an order_index arriving that's already held)
    // so the disconnect log can tell the two apart instead of one
    // number silently meaning either thing.
    uint32_t  diag_order_forced_skip;
    // Packets accepted despite arriving on an already-used order index,
    // i.e. a client that reuses one order index for the whole session.
    uint32_t  diag_order_index_reuse;
    // Reliable messages dropped as genuine retransmits (same message index
    // AND same bytes). Surfaced in the disconnect dump so a network that
    // keeps retransmitting is visible, while only the FIRST one per session
    // is printed to the console — otherwise a retransmit-happy link floods
    // stderr with one line per duplicate and buries the real log.
    uint32_t  diag_duplicate_dropped;
    bool      diag_duplicate_logged;

    // MTU negotiated during handshake
    uint16_t  mtu;

    // Timing
    // last_activity_ms is the canonical "does this session keep its
    // connection" clock: ANY datagram received touches it — keepalive
    // PINGs, ACK/NACKs and real game packets alike. This matches RakLib
    // (Connection::lastUpdate) and fixes the AFK kick: a player who is
    // idle but still connected only emits keepalives, and must never be
    // dropped for it. The fields below are purely diagnostic breakdowns
    // (last_game_packet_received_ms distinguishes a truly silent session
    // from an idle-but-alive one in disconnect logs).
    uint64_t  last_activity_ms;
    uint64_t  last_packet_received_ms;      // any RakNet datagram at all
    uint64_t  last_game_packet_received_ms; // non-PING encapsulated packet
    uint64_t  last_ping_received_ms;
    uint64_t  last_packet_sent_ms;
    uint64_t  start_time_ms;
    bool      is_active;        // received at least one packet this tick

    // splitID counter for outbound fragmented packets
    uint16_t  split_id_counter;

    // Bug 2 fix: set when CLIENT_DISCONNECT (0x15) is received.
    // The session is cleaned up on the next tick sweep — no double-close.
    bool      is_disconnecting;
} mcp_rak_session_t;

// Init / destroy
void mcp_rak_session_init(mcp_rak_session_t* s, const uint8_t ip[4],
                           uint16_t port, int64_t client_id, int64_t server_id);
void mcp_rak_session_destroy(mcp_rak_session_t* s);

// ACK handling
void   mcp_rak_session_queue_ack(mcp_rak_session_t* s, uint32_t seq);
void   mcp_rak_session_queue_nack(mcp_rak_session_t* s, uint32_t seq);
size_t mcp_rak_session_drain_ack_queue(mcp_rak_session_t* s,
                                        uint32_t* out_mins, uint32_t* out_maxs,
                                        size_t out_cap);
size_t mcp_rak_session_drain_nack_queue(mcp_rak_session_t* s,
                                         uint32_t* out_mins, uint32_t* out_maxs,
                                         size_t out_cap);

// Overhead constants (bytes) for computing fragment sizes.
// Datagram header:  1 (type 0x84) + 3 (seq LE24) = 4
// Encap no-split:   1 (flags) + 2 (bit-len BE16) + 3 (msgIdx LE24)
//                 + 3 (ordIdx LE24) + 1 (ordChan) = 10
// Split fields:     2 (splitID BE16) + 4 (splitIdx BE32) + 4 (splitCount BE32) = 10
#define MCP_DATAGRAM_HEADER_BYTES  4
#define MCP_ENCAP_BASE_BYTES      10   // flags+bitlen+msgIdx+ordIdx+ordChan
#define MCP_ENCAP_SPLIT_BYTES     10   // splitID+splitIndex+splitCount
// Per-datagram transport overhead that is NOT part of s->mtu.
// s->mtu is the maximum IP PACKET size (that is what SessionManager.c
// advertises in UnconnectedPing: `mtu = len + 28`, i.e. the bytes we sent
// plus exactly one IPv4 + one UDP header). The send path has to subtract
// both headers before it fits any RakNet payload into it.
//
// BUG THIS FIXES ("stuck on Sincronizando paquetes", Linux host only):
// the send path treated s->mtu as if it were the UDP payload budget, so a
// full-size fragment was emitted as 1488 bytes of UDP payload = 1516 bytes
// on the wire (1488 + 8 UDP + 20 IPv4). That is 16 bytes OVER the 1500-byte
// Ethernet/IPv4 MTU. Linux sets the don't-fragment bit on UDP datagrams by
// default (net.ipv4.ip_no_pmtu_disc=0) and DROPS an oversized datagram
// silently instead of fragmenting it, so every split fragment was lost and
// no chunk ever arrived — the client sat on the loading screen forever
// while small packets (chat, keepalive, container updates) kept working.
// Android survived because its UDP/PMTUD path copes differently on a plain
// 1500-MTU Wi-Fi link.
#define MCP_IPV4_HEADER_BYTES     20
#define MCP_UDP_HEADER_BYTES       8

// Encapsulation: wrap a MCPE payload inside a RakNet encapsulated record
// inside a DataPacket_4 (0x84) datagram. Returns bytes written, or 0 on error.
// INTERNAL — builds exactly one datagram for the given payload slice.
// For payloads that may exceed MTU, call mcp_rak_session_send_payload instead.
size_t mcp_rak_session_build_datagram(mcp_rak_session_t* s,
                                       uint8_t* out, size_t out_cap,
                                       const uint8_t* payload, size_t plen,
                                       uint8_t reliability, uint8_t order_channel,
                                       bool has_split, uint16_t split_id,
                                       uint32_t split_index, uint32_t split_count);

// High-level send: splits payload into MTU-sized fragments if needed,
// builds a datagram per fragment, and sends each via the socket.
// This is the correct function to call for all game-level packet sends.
// Returns true if all fragments were sent successfully.
bool mcp_rak_session_send_payload(mcp_rak_session_t* s,
                                   mcp_rak_session_manager_t* sm,
                                   const uint8_t* payload, size_t plen,
                                   uint8_t reliability, uint8_t order_channel);

// Enqueue a datagram for sending; flushed by tick.
bool mcp_rak_session_enqueue(mcp_rak_session_t* s,
                              const uint8_t* data, size_t len);

// Timeout check
bool mcp_rak_session_is_timed_out(const mcp_rak_session_t* s,
                                    uint64_t now_ms, uint64_t timeout_ms);

#ifdef __cplusplus
} // extern "C"
#endif
