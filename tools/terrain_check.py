#!/usr/bin/env python3
"""Terrain check: how many FullChunkDataPackets really reach the client.

Reuses the real RakNet client from tools/linux_login_check.py (so the whole
handshake + RakLib split reassembly is exercised for real), but replaces its
batch unwrapper with one that records EVERY game sub-packet from the very
first datagram, then keeps listening and reports:

  * how many distinct (cx, cz) arrived, which ones of the configured view
    radius are MISSING (the one under the player's feet matters most — no
    chunk there means the client falls out of the world and sees nothing),
  * a per-second timeline of distinct chunks (a reset there means the
    server keeps re-sending the whole view from scratch),
  * how many sub-packets each batch carried, split-fragments per chunk and
    the compressed batch size.

--ack N makes the harness behave like a real client and answer the server's
PlayStatus(PLAYER_SPAWN) with its own PlayStatus(PLAYER_SPAWN), which is
what drives Player::handleClientSpawnAck().

Usage:  python3 tools/terrain_check.py [--seconds 12] [--ack 1]
"""
import argparse
import collections
import os
import struct
import sys
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from linux_login_check import (  # noqa: E402
    Client, PLAY_STATUS, FULL_CHUNK_DATA, BATCH_PACKET_ID,
    ACK, NACK, UNCONNECTED_PONG, OCR1_REPLY, OCR2_REPLY,
    STAGE_LOGIN, STAGE_SPAWN, STAGE_CHUNKS,
    DATA_FIRST, DATA_LAST,
)

PLAYER_SPAWN = 3    # PlayStatusPacket::PLAYER_SPAWN (protocol 84)
RADIUS = 8          # default cfg_.view_distance in Player.h


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    ap.add_argument("--name", default="TerrainTester")
    ap.add_argument("--seconds", type=float, default=12.0)
    ap.add_argument("--ack", type=int, default=1)
    ap.add_argument("--ack-every", type=float, default=0.0,
                    help="if >0, send one ACK every N seconds instead of "
                         "all at once (a real client ACKs once it has "
                         "actually finished loading the world)")
    ap.add_argument("--radius", type=int, default=RADIUS)
    args = ap.parse_args()

    c = Client(args.host, args.port, args.name)

    seen = set()
    dup = [0]
    frags = collections.Counter()
    subs_per_batch = collections.Counter()
    comp = []
    play_status = []
    pids = collections.Counter()
    acks = [0]
    arrival = []          # (t, pid) in wire order — a real client is order-sensitive
    t0 = [time.time()]

    def dispatch(p):
        if not p:
            return
        pids[p[0]] += 1
        arrival.append((time.time() - t0[0], p[0]))
        if p[0] == FULL_CHUNK_DATA and len(p) >= 13:
            cx, cz = struct.unpack("<ii", p[1:9])
            key = (cx, cz)
            if key in seen:
                dup[0] += 1
            seen.add(key)
        elif p[0] == PLAY_STATUS and len(p) >= 5:
            play_status.append(int.from_bytes(p[1:5], "big"))

    def unwrap(payload):
        """Full batch unwrap; records every sub-packet (not just the first)."""
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == BATCH_PACKET_ID and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                blob = body[5:5 + clen]
                try:
                    raw = zlib.decompress(blob)
                except zlib.error:
                    print(f"  !! zlib error, compressedLen={clen} got={len(blob)}")
                    return
                comp.append(clen)
                off, subs = 0, []
                while off + 4 <= len(raw):
                    sl = int.from_bytes(raw[off:off + 4], "big")
                    off += 4
                    if sl == 0 or off + sl > len(raw):
                        break
                    subs.append(raw[off:off + sl])
                    off += sl
                subs_per_batch[len(subs)] += 1
                for s in subs:
                    dispatch(s)
                return
        dispatch(payload)

    def hooked_handle_payload(payload):
        """Records everything AND keeps the base class's stage tracking."""
        Client.handle_payload(c, payload)
        unwrap(payload)

    # Hook BEFORE the handshake so nothing is lost during login/expect.
    c.handle_payload = hooked_handle_payload
    c.terrain_seen = seen
    c.terrain_acks = acks

    failed = c.handshake()
    if failed:
        print(f"FAIL: handshake stage '{failed}'")
        return 1
    failed = c.login()
    if failed:
        print(f"FAIL: login stage '{failed}'")
        return 1

    def send_spawn_ack():
        # [pid 0x02][Int32 BE status] — the server's PlayStatusPacket::decode
        # reads a full 4-byte BE int, so a 2-byte packet is rejected.
        body = bytes([PLAY_STATUS]) + struct.pack(">I", PLAYER_SPAWN)
        c.send_raw(c.send_encapsulated(body, reliability=3, order_channel=0))
        acks[0] += 1
        print(f"  [t={time.time()-start:5.1f}s] sent spawn ACK #{acks[0]}")

    start = time.time()
    deadline = start + args.seconds
    last_tl, last_n = start, 0
    last_ack_t = start
    timeline = []
    while time.time() < deadline:
        try:
            data = c.sock.recvfrom(4096)[0]
        except (OSError, socket_timeout()):
            data = None
        now = time.time()
        if data and data[0] not in (ACK, NACK, UNCONNECTED_PONG, OCR1_REPLY, OCR2_REPLY) \
                and DATA_FIRST <= data[0] <= DATA_LAST:
            rel = (data[4] >> 5) & 0x07
            if rel in (2, 3, 4, 6, 7):
                c.send_ack(data[1] | (data[2] << 8) | (data[3] << 16))
            for payload, is_split, count, sid, idx in c.parse_datagram(data):
                if not payload:
                    continue
                if is_split:
                    whole = c.add_fragment(payload, count, sid, idx)
                    if whole:
                        hooked_handle_payload(whole)
                    continue
                hooked_handle_payload(payload)
        if args.ack > 0 and PLAYER_SPAWN in play_status and acks[0] < args.ack:
            due = (acks[0] == 0) if args.ack_every <= 0 else \
                  (now - last_ack_t >= args.ack_every)
            if due:
                send_spawn_ack()
                last_ack_t = now
        if now - last_tl >= 1.0:
            timeline.append((round(now - start, 1), len(seen), len(seen) - last_n))
            last_n, last_tl = len(seen), now

    elapsed = time.time() - start
    print(f"elapsed                : {elapsed:.1f}s")
    print(f"PlayStatus values seen : {play_status[:16]}")
    print(f"spawn ACKs sent        : {acks[0]}")
    print(f"distinct (cx,cz)       : {len(seen)}")
    print(f"duplicate chunk sends  : {dup[0]}")
    print(f"timeline (t,distinct,+/s): {timeline}")
    print(f"fragments per chunk    : {dict(sorted(frags.items()))}")
    print(f"sub-packets per batch  : {dict(sorted(subs_per_batch.items()))}")
    print(f"game packet ids        : {dict(sorted(pids.items()))}")
    names = {0x02: "PlayStatus", 0x09: "StartGame", 0x34: "FullChunk",
             0x8F: "Crafting", 0x2E: "Adventure", 0x36: "SetEntityData",
             0x3F: "ContainerContent", 0x12: "AddPlayer", 0x2A: "MovePlayer"}
    head = [f"{names.get(pid, hex(pid))}@{t:.2f}" for t, pid in arrival[:26]]
    print(f"wire order (first 26)  : {head}")
    # Where did the first chunk land relative to the handshake?
    def first(pid):
        for t, q in arrival:
            if q == pid:
                return t
        return None
    fg, fs, fc = first(0x34), first(0x09), first(0x02)
    print(f"first PlayStatus@{fg}  StartGame@{fs}  first FullChunk@{fc}")
    if fc is not None and fs is not None:
        print("VERDICT: " + ("chunks arrive BEFORE StartGame (client discards them)"
                            if fc < fs else "StartGame arrives before chunks (ok)"))
    if comp:
        cl = sorted(comp)
        print(f"batch compressed bytes : min={cl[0]} med={cl[len(cl)//2]} max={cl[-1]}")

    if seen:
        xs = [a for a, _ in seen]
        zs = [b for _, b in seen]
        cx0, cz0 = min(xs), min(zs)
        expected = {(cx0 + dx, cz0 + dz)
                    for dx in range(-args.radius, args.radius + 1)
                    for dz in range(-args.radius, args.radius + 1)}
        missing = sorted(expected - seen)
        ring3 = [(cx0 + dx, cz0 + dz)
                 for dx in (-1, 0, 1) for dz in (-1, 0, 1)]
        print(f"view corner            : ({cx0},{cz0}) expected={len(expected)}")
        print(f"centre chunk present   : {(cx0, cz0) in seen}")
        print(f"3x3 at corner ok       : {sum(1 for r in ring3 if r in seen)}/9")
        print(f"MISSING chunks         : {len(missing)} {missing[:40]}")
    if not seen:
        print("FAIL: NO FullChunkDataPacket arrived")
        return 2
    print("OK: chunks arrived")
    return 0


def socket_timeout():
    import socket
    return socket.timeout


if __name__ == "__main__":
    sys.exit(main())