#!/usr/bin/env python3
"""End-to-end nether-portal travel check for a local CurseStone server.

Needs the PortalTest plugin (builds an obsidian frame near the spawn) and
an ops.txt entry for the test player. Sequence, all through the real
protocol-84 wire:

  1. login -> /gamemode 1 -> creative-tap a flint & steel (slot 0)
  2. /tp onto the frame platform, USE_ITEM the base row -> portal lights
  3. step inside -> ChangeDimension(1) to the nether (portal auto-built)
  4. step into the arrival portal -> ChangeDimension(0) back, landing at
     the ORIGINAL overworld portal (registry round trip)

Usage:
    python3 tools/portal_travel_check.py [--host 127.0.0.1] [--port 19132]
"""
import argparse
import queue
import socket
import struct
import sys
import threading
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from fall_damage_check import FallClient, bestr  # noqa: E402
from linux_login_check import STAGE_SPAWN, STAGE_CHUNKS  # noqa: E402

MOVE_PLAYER = 0x10
USE_ITEM = 0x1F
CONTAINER_SET_SLOT = 0x2C
CHANGE_DIMENSION = 0x36
UPDATE_BLOCK = 0x13
PLAY_STATUS = 0x02

# Frame layout (must match plugins/PortalTest/src/main.lua).
FRAME = dict(W=130, Y=71, Z=130)


class PortalClient(FallClient):
    """Login client with a dedicated RX thread: ACKs and split
    reassembly never stall while the main thread sleeps, which is what
    keeps the server's retransmit flood from drowning the session."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.last_reset = None      # last MOVE_PLAYER mode=RESET (x,y,z)
        self.dimensions = []        # CHANGE_DIMENSION dimension values
        self.portal_blocks = 0      # UPDATE_BLOCK carrying block 90
        self.unacked = {}           # raknet seq -> (raw datagram, sent_ms)
        self.rxq = queue.Queue()
        self._stop = False

    # ---- minimal reliable delivery: track + retransmit on NACK/RTO ----
    def send_game(self, inner: bytes):
        raw = self.send_encapsulated(inner, reliability=3)
        seq = self.send_seq - 1
        self.unacked[seq] = (raw, time.time())
        self.send_raw(raw)

    def _parse_records(self, data: bytes, off: int):
        """RakNet ACK/NACK record list: count u16, then single/range recs."""
        if off + 2 > len(data):
            return []
        count = int.from_bytes(data[off:off + 2], "big")
        off += 2
        seqs = []
        for _ in range(count):
            if off >= len(data):
                break
            kind = data[off]
            off += 1
            if kind == 0x01:
                seqs.append(int.from_bytes(data[off:off + 3], "little"))
                off += 3
            elif kind == 0x00 and off + 6 <= len(data):
                lo = int.from_bytes(data[off:off + 3], "little")
                hi = int.from_bytes(data[off + 3:off + 6], "little")
                seqs.extend(range(lo, hi + 1))
                off += 6
        return seqs

    def _handle_datagram(self, data: bytes):
        if not data:
            return
        if data[0] == 0xC0:                      # ACK
            for seq in self._parse_records(data, 1):
                self.unacked.pop(seq, None)
            return
        if data[0] == 0xA0:                      # NACK -> resend now
            now = time.time()
            for seq in self._parse_records(data, 1):
                if seq in self.unacked:
                    raw, _ = self.unacked[seq]
                    self.send_raw(raw)
                    self.unacked[seq] = (raw, now)
            return
        if 0x80 <= data[0] <= 0x8F:
            rel = (data[4] >> 5) & 0x07
            if rel in (2, 3, 4, 6, 7):
                self.send_ack(data[1] | (data[2] << 8) | (data[3] << 16))
        for payload, is_split, count, sid, idx in self.parse_datagram(data):
            if is_split:
                whole = self.add_fragment(payload, count, sid, idx)
                if whole:
                    self.rxq.put(whole)
            else:
                self.rxq.put(payload)

    def start_rx(self):
        def loop():
            self.sock.settimeout(0.3)
            while not self._stop:
                try:
                    data, _ = self.sock.recvfrom(65536)
                except socket.timeout:
                    # RTO: retransmit anything unacknowledged for >1s.
                    now = time.time()
                    for seq in [s for s, (_, t) in self.unacked.items()
                                if now - t > 1.0]:
                        raw, _ = self.unacked[seq]
                        self.send_raw(raw)
                        self.unacked[seq] = (raw, now)
                    continue
                except OSError:
                    break
                self._handle_datagram(data)
        t = threading.Thread(target=loop, daemon=True)
        t.start()
        return t

    def watch(self, seconds: float):
        deadline = time.time() + seconds
        while time.time() < deadline:
            try:
                self.classify(self.rxq.get(timeout=0.2))
            except queue.Empty:
                pass

    def classify(self, payload: bytes):
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:
            body = payload[1:]
            if body[0] == 0x06 and len(body) > 5:
                clen = int.from_bytes(body[1:5], "big")
                try:
                    raw = zlib.decompress(body[5:5 + clen])
                except zlib.error:
                    return
                i = 0
                while i + 4 < len(raw):
                    sublen = int.from_bytes(raw[i:i + 4], "big")
                    sub = raw[i + 4:i + 4 + sublen]
                    self.classify(sub)
                    i += 4 + sublen
                return
        if payload[0] == MOVE_PLAYER and len(payload) >= 34:
            mode = payload[33]
            if mode == 1:
                x, y, z = struct.unpack(">fff", payload[9:21])
                self.last_reset = (x, y, z)
        elif payload[0] == CHANGE_DIMENSION and len(payload) >= 14:
            self.dimensions.append(payload[1])
        elif payload[0] == UPDATE_BLOCK and len(payload) >= 10:
            # wire: x(i32) z(i32) y(u8) blockId(u8) flags/data(u8)
            if payload[9] == 90:   # nether portal block reached the wire
                self.portal_blocks += 1
        FallClient.classify(self, payload)

    # -- helpers --
    def move(self, x, y_eye, z, on_ground=True):
        mp = bytes([MOVE_PLAYER]) + struct.pack(">q", 1)
        mp += struct.pack(">fff", x, y_eye, z)
        mp += struct.pack(">fff", 90.0, 90.0, 0.0)
        mp += bytes([0, 1 if on_ground else 0])
        self.send_game(mp)

    def say(self, msg):
        self.send_game(bytes([0x07, 1]) + bestr(self.name) + bestr(msg))

    def creative_take(self, slot, item_id):
        cs = bytes([CONTAINER_SET_SLOT])
        cs += bytes([0])                      # windowid 0 (inventory)
        cs += struct.pack(">h", slot)         # slot
        cs += struct.pack(">h", 0)            # hotbarSlot
        cs += struct.pack(">h", item_id)
        cs += bytes([1])                      # count
        cs += struct.pack(">h", 0)            # damage
        cs += struct.pack("<H", 0)            # no nbt
        self.send_game(cs)

    def use_item_on(self, tx, ty, tz, face, px, py, pz, item_id):
        ui = bytes([USE_ITEM])
        ui += struct.pack(">iii", tx, ty, tz)   # clicked block
        ui += bytes([face])
        ui += struct.pack(">fff", 0.5, 1.0, 0.5)
        ui += struct.pack(">fff", px, py, pz)   # player position
        ui += struct.pack(">i", 0)              # slot
        ui += struct.pack(">h", item_id)
        ui += bytes([1])                        # count
        ui += struct.pack(">h", 0)              # damage
        ui += struct.pack("<H", 0)              # no nbt
        self.send_game(ui)


def login(host, port, name):
    for attempt in range(3):
        c = PortalClient(host, port, name)
        if c.handshake():
            print(f"[{name}] handshake retry {attempt + 1}")
            continue
        if c.login():
            print(f"[{name}] login retry {attempt + 1}")
            continue
        if not c.expect(STAGE_SPAWN, seconds=6.0):
            print(f"[{name}] spawn retry {attempt + 1}")
            continue
        c.expect(STAGE_CHUNKS, seconds=5.0)
        c.start_rx()   # dedicated receiver keeps ACKs flowing
        c.watch(1.0)
        return c
    print(f"FAIL[{name}]: could not complete login")
    return None


def run(host: str, port: int) -> int:
    name = "FallPort"
    c = login(host, port, name)
    if not c:
        return 1
    print(f"[{name}] login OK; preparing creative flint & steel...")

    c.say("/gamemode 1")
    time.sleep(0.8)
    c.creative_take(0, 259)          # flint & steel into the hand
    time.sleep(0.5)

    W, Y, Z = FRAME["W"], FRAME["Y"], FRAME["Z"]
    c.say(f"/teleport {W + 1.5} {Y} {Z + 1.5}")   # platform in front of frame
    time.sleep(1.0)
    c.watch(1.0)
    print(f"[{name}] teleport result pos: {c.last_reset}")
    c.move(W + 1.5, Y + 1.62, Z + 1.5)            # feet on the front floor
    time.sleep(0.3)

    # Light the portal: click the base row of the frame.
    c.use_item_on(W + 1, Y, Z, 1, W + 1.5, Y + 1.62, Z + 1.5, 259)
    time.sleep(1.0)
    c.watch(1.5)
    print(f"[{name}] portal blocks on wire: {c.portal_blocks} "
          f"unacked={len(c.unacked)}")

    # Step into the portal interior (feet on top of the base row).
    c.move(W + 1.5, Y + 2 + 1.62, Z + 0.5)
    print(f"[{name}] stepping into the portal (overworld -> nether)...")
    c.watch(6.0)
    if 1 not in c.dimensions:
        print(f"FAIL: no ChangeDimension(1) seen; dims={c.dimensions}")
        return 2
    nether_pos = c.last_reset
    print(f"[{name}] arrived in nether at {nether_pos}")

    # Re-enter the arrival portal: walk one block towards its plane.
    if not nether_pos:
        print("FAIL: no arrival position")
        return 3
    ax, ay, az = nether_pos
    c.move(ax, ay, az - 1.0)         # interior plane sits one block -Z
    print(f"[{name}] stepping back through the arrival portal...")
    time.sleep(0.5)
    c.move(ax, ay, az - 1.0)
    c.watch(6.0)
    if 0 not in c.dimensions:
        print(f"FAIL: no ChangeDimension(0) seen; dims={c.dimensions}")
        return 4
    back_pos = c.last_reset
    print(f"[{name}] back in overworld at {back_pos}")

    # Round trip: the return must land at the ORIGINAL portal (the frame
    # platform), which the coordinate search finds in the registry.
    dx = abs(back_pos[0] - (W + 1.5))
    dz = abs(back_pos[2] - (Z + 1.5))
    ok = dx <= 4.0 and dz <= 4.0
    print(f"[{name}] round-trip offset dx={dx:.2f} dz={dz:.2f} "
          f"portal_blocks={c.portal_blocks} -> {'OK' if ok else 'FAIL'}")
    return 0 if ok else 5


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="CurseStone portal travel E2E")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port))
