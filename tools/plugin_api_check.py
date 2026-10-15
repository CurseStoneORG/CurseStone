#!/usr/bin/env python3
"""End-to-end plugin-API check for a local CurseStone server (Linux).

Extends the login harness: after a real RakNet handshake + login + spawn it
sends the game packets that drive the new plugin events and control calls,
then reports what reached the wire. Pair it with the TestRes plugin, whose
log lines prove the event handlers fired server-side.

Sequence sent after spawn:
  1. MOVE_PLAYER (move + rotate)      -> PlayerMove event
  2. MOB_EQUIPMENT (hotbar slot 1)    -> PlayerItemHeld event
  3. USE_ITEM face=0xFF (item in air) -> PlayerInteract right_click_air
  4. DROP_ITEM type=0                 -> PlayerDropItem event
Then it waits for the server-driven packets:
  - MOVE_PLAYER mode=RESET            -> setRotation camera sync
  - SET_ENTITY_MOTION eid=0           -> setMotion push
(the TestRes plugin fires both from a scheduler task every 20 ticks).
"""
import argparse
import struct
import sys
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from linux_login_check import (Client, STAGE_SPAWN, STAGE_CHUNKS)  # noqa: E402

MOVE_PLAYER = 0x10
MOB_EQUIPMENT = 0x1B
USE_ITEM = 0x1F
SET_ENTITY_MOTION = 0x23
DROP_ITEM = 0x29

STAGE_EVENT_PACKETS = "event-packets-sent"
STAGE_GOT_RESET = "got-moveplayer-reset"
STAGE_GOT_MOTION = "got-self-motion"


class EventClient(Client):
    """Client that can send the game packets driving the new plugin API."""

    def handle_payload(self, payload: bytes):
        """Unwrap batches with MULTIPLE sub-packets (the base class reads
        only the first one, which misses StartGame when it rides in the
        same batch as recipes)."""
        if payload and payload[0] == 0xFE and len(payload) > 5:
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
                    Client.handle_payload(self, sub)
                    i += 4 + sublen
                return
        Client.handle_payload(self, payload)

    def send_game(self, inner: bytes):
        self.send_raw(self.send_encapsulated(inner, reliability=3))

    def watch_server_packets(self, seconds: float):
        """Drain and flag the server-driven packets we care about."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            data = self.recv_one()
            if data is None:
                continue
            if data[0] in (0xC0, 0xA0):
                continue
            if 0x80 <= data[0] <= 0x8F:
                rel = (data[4] >> 5) & 0x07
                if rel in (2, 3, 4, 6, 7):
                    self.send_ack(data[1] | (data[2] << 8) | (data[3] << 16))
            for payload, is_split, count, sid, idx in self.parse_datagram(data):
                if is_split:
                    whole = self.add_fragment(payload, count, sid, idx)
                    if whole:
                        self.classify(whole)
                else:
                    self.classify(payload)

    def classify(self, payload: bytes):
        if not payload:
            return
        if payload[0] == 0xFE and len(payload) > 5:  # batch unwrap
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
        gp = payload[0]
        self.recv_ids.append(gp)
        if gp == MOVE_PLAYER and len(payload) >= 34:
            # wire: pid + eid(8) + x,y,z(12) + yaw,bodyYaw,pitch(12) + mode
            mode = payload[33]
            if mode == 1:
                self.reached.add(STAGE_GOT_RESET)
        elif gp == SET_ENTITY_MOTION and len(payload) >= 17:
            eid = struct.unpack(">q", payload[1:9])[0]
            if eid == 0:
                self.reached.add(STAGE_GOT_MOTION)


def run(host: str, port: int, name: str) -> int:
    c = EventClient(host, port, name)
    failed = c.handshake()
    if failed:
        print(f"FAIL: never completed stage '{failed}'")
        return 1
    failed = c.login()
    if failed:
        print(f"FAIL: login not answered (stage '{failed}')")
        return 2
    if not c.expect(STAGE_SPAWN, seconds=6.0):
        print("FAIL: no StartGame (no spawn)")
        return 3
    c.expect(STAGE_CHUNKS, seconds=5.0)
    print("login + spawn OK; sending event-driving packets...")

    eid = 1  # server-side players use runtime id 1 (first allocated)

    # 1. MOVE_PLAYER: small walk + rotate (drives PlayerMove; a big jump
    # would hit the anti-cheat speed cap and be rejected instead).
    mp = bytes([MOVE_PLAYER]) + struct.pack(">q", eid)
    mp += struct.pack(">fff", 129.0, 66.0 + 1.62, 128.4)
    mp += struct.pack(">fff", 90.0, 12.0, 90.0)   # yaw, bodyYaw, pitch
    mp += bytes([0, 1])                            # mode=normal, onGround
    c.send_game(mp)

    # 2. MOB_EQUIPMENT: item slot + hotbarSlot + selectedSlot (drives
    # PlayerItemHeld). Empty item (id 0) — the hotbar tap is what matters.
    me = bytes([MOB_EQUIPMENT]) + struct.pack(">q", eid)
    me += struct.pack(">h", 0)                     # item id 0 = empty slot
    me += bytes([1])                               # hotbarSlot
    me += bytes([1])                               # selectedSlot -> 1
    c.send_game(me)

    # 3. USE_ITEM face=0xFF with empty hand: use item in the air (drives
    # right_click_air).
    ui = bytes([USE_ITEM])
    ui += struct.pack(">iii", 100, 100, 100)       # aim direction * 32768
    ui += bytes([0xFF])                             # face = air
    ui += struct.pack(">fff", 0.5, 0.5, 0.5)       # fx, fy, fz
    ui += struct.pack(">fff", 0.0, 0.0, 0.0)       # posX, posY, posZ
    ui += struct.pack(">i", 0)                      # slot
    ui += struct.pack(">h", 0)                      # item id 0 = empty hand
    c.send_game(ui)

    # 4a. DROP_ITEM of the BEDROCK stack (id 7): the TestRes plugin
    # cancels bedrock drops, so the server must NOT spawn an item
    # entity (no [DROP] log line) and must resync the slot.
    di7 = bytes([DROP_ITEM])
    di7 += bytes([0])
    di7 += struct.pack(">h", 7)
    di7 += bytes([1])
    di7 += struct.pack(">h", 0)
    di7 += struct.pack("<H", 0)
    c.send_game(di7)
    time.sleep(0.5)

    # 4b. DROP_ITEM of the iron ingot stack (id 265): allowed — this one
    # must produce exactly one dropped item entity.
    di = bytes([DROP_ITEM])
    di += bytes([0])                               # type: drop one
    di += struct.pack(">h", 265)                   # iron ingot
    di += bytes([1])                               # count
    di += struct.pack(">h", 0)                     # damage
    di += struct.pack("<H", 0)                     # nbtLen
    c.send_game(di)

    c.reached.add(STAGE_EVENT_PACKETS)

    # The TestRes plugin has a scheduler task that calls
    # player.setRotation + player.setMotion on us every second.
    c.watch_server_packets(6.0)

    print(f"stages reached  : {sorted(c.reached)}")
    print(f"game packet ids : {sorted(set(c.recv_ids))}")
    ok = STAGE_EVENT_PACKETS in c.reached
    if not ok:
        print("FAIL: could not send the event packets")
        return 4
    if STAGE_GOT_RESET not in c.reached:
        print("FAIL: no MOVE_PLAYER RESET received (setRotation sync missing)")
        return 5
    if STAGE_GOT_MOTION not in c.reached:
        print("FAIL: no SET_ENTITY_MOTION eid=0 received (setMotion missing)")
        return 6
    print("OK: event packets accepted + setRotation/setMotion wire sync seen")
    return 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="CurseStone plugin API E2E check")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    ap.add_argument("--name", default="LinuxTester")
    args = ap.parse_args()
    sys.exit(run(args.host, args.port, args.name))
