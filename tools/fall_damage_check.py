#!/usr/bin/env python3
"""End-to-end fall-damage check for a local CurseStone server (Linux).

Drives the real RakNet + protocol-84 path with a simulated client to prove
the gamemode/fall-damage rules:

  scenario 1: a fall that starts AND ends in survival applies vanilla
              damage (descent - 3) exactly once.
  scenario 2: a fall performed in creative must not accumulate; after a
              mid-air switch to survival only the survival part of the
              descent may hurt (and it must NOT kill).

Usage:
    python3 tools/fall_damage_check.py [--host 127.0.0.1] [--port 19132]
"""
import argparse
import struct
import sys
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from linux_login_check import (Client, STAGE_SPAWN, STAGE_CHUNKS)  # noqa: E402

MOVE_PLAYER = 0x10
TEXT_PACKET = 0x07
ENTITY_EVENT = 0x18
UPDATE_ATTRIBUTES = 0x1A
HURT_ANIMATION = 2

EYE = 1.62
GROUND_EYE = 66.0 + EYE   # matches the spawn-area surface other tools use
HIGH_EYE = 90.0 + EYE


def bestr(s: str) -> bytes:
    raw = s.encode()
    return struct.pack(">H", len(raw)) + raw


class FallClient(Client):
    """Login client that tracks health attributes + hurt events."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.health = 20.0
        self.min_health = 20.0
        self.hurts = 0

    # ---- plumbing (same as plugin_api_check) ----
    def handle_payload(self, payload: bytes):
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

    def watch(self, seconds: float):
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

    # ---- classification ----
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
        if payload[0] == ENTITY_EVENT and len(payload) >= 10:
            eid = struct.unpack(">q", payload[1:9])[0]
            if payload[9] == HURT_ANIMATION:
                self.hurts += 1
            self.recv_ids.append(("ee", eid, payload[9]))
        elif payload[0] == UPDATE_ATTRIBUTES and len(payload) >= 11:
            count = struct.unpack(">H", payload[9:11])[0]
            off = 11
            for _ in range(count):
                if off + 12 > len(payload):
                    break
                minv, maxv, value = struct.unpack(
                    ">fff", payload[off:off + 12])
                off += 12
                nlen = struct.unpack(">H", payload[off:off + 2])[0]
                off += 2
                name = payload[off:off + nlen].decode("utf-8", "replace")
                off += nlen
                if name == "generic.health":
                    self.health = value
                    self.min_health = min(self.min_health, value)

    # ---- movement helpers ----
    def move(self, x, y_eye, z, on_ground):
        mp = bytes([MOVE_PLAYER]) + struct.pack(">q", 1)
        mp += struct.pack(">fff", x, y_eye, z)
        mp += struct.pack(">fff", 90.0, 90.0, 0.0)
        mp += bytes([0, 1 if on_ground else 0])
        self.send_game(mp)

    def vertical(self, y_from, y_to, on_ground=False):
        """Climb/descend in steps small enough for the anti-cheat cap."""
        step = 4.0 if y_to > y_from else -4.0
        y = y_from
        while abs(y_to - y) > 4.0:
            y += step
            self.move(129.0, y, 128.4, on_ground)
            time.sleep(0.06)
        self.move(129.0, y_to, 128.4, on_ground)
        time.sleep(0.06)

    def say(self, msg):
        self.send_game(bytes([TEXT_PACKET, 1]) + bestr(self.name) + bestr(msg))


def spawn(c: FallClient, name: str) -> bool:
    if c.handshake():
        print(f"FAIL[{name}]: handshake never completed")
        return False
    if c.login():
        print(f"FAIL[{name}]: login not answered")
        return False
    if not c.expect(STAGE_SPAWN, seconds=6.0):
        print(f"FAIL[{name}]: no StartGame (no spawn)")
        return False
    c.expect(STAGE_CHUNKS, seconds=5.0)
    return True


def scenario_pure_survival(host, port) -> bool:
    name = "FallSurv"
    c = FallClient(host, port, name)
    if not spawn(c, name):
        return False
    print(f"[{name}] login OK; falling 15 blocks in survival...")

    c.watch(1.0)   # settle; baseline health 20
    c.min_health = 20.0
    c.hurts = 0

    c.move(129.0, GROUND_EYE, 128.4, True)
    time.sleep(0.2)
    c.vertical(GROUND_EYE, HIGH_EYE)            # climb (rising: baseline reset)
    c.vertical(HIGH_EYE, 75.0 + EYE)            # descend 15 blocks
    c.move(129.0, GROUND_EYE, 128.4, True)      # land
    c.watch(2.5)

    # vanilla: damage = 15 - 3 = 12  ->  health 20 -> 8
    ok = 7.0 <= c.min_health <= 9.5
    evs = [e for e in c.recv_ids if isinstance(e, tuple)][:6]
    print(f"[{name}] hurts={c.hurts} min_health={c.min_health:.1f} "
          f"events={evs} -> {'OK' if ok else 'FAIL'}")
    return ok


def scenario_creative_midair_switch(host, port) -> bool:
    name = "FallMixed"
    c = FallClient(host, port, name)
    if not spawn(c, name):
        return False
    print(f"[{name}] login OK; creative fall + mid-air switch to survival...")

    c.watch(1.0)
    c.min_health = 20.0
    c.hurts = 0

    c.move(129.0, GROUND_EYE, 128.4, True)
    time.sleep(0.2)
    c.say("/gamemode 1")                        # creative
    time.sleep(0.8)
    c.vertical(GROUND_EYE, HIGH_EYE)            # climb while flying
    c.vertical(HIGH_EYE, 75.0 + EYE)            # fall 15 blocks in creative
    c.say("/gamemode 0")                        # switch mid-air
    time.sleep(0.8)
    c.vertical(75.0 + EYE, 70.0 + EYE)          # survival descent: 5 blocks
    c.move(129.0, GROUND_EYE, 128.4, True)      # land
    c.watch(2.5)

    # The creative part must not count: only the 5 survival blocks apply
    # -> damage 2 -> health 18. The old behaviour accumulated the whole
    # fall (20 blocks -> damage 17 -> health 3).
    ok = 17.0 <= c.min_health <= 19.0
    evs = [e for e in c.recv_ids if isinstance(e, tuple)][:6]
    print(f"[{name}] hurts={c.hurts} min_health={c.min_health:.1f} "
          f"events={evs} -> {'OK' if ok else 'FAIL'} "
          f"(expected ~18, old bug gave 3)")
    return ok


def run(host: str, port: int) -> int:
    print("== scenario 1: survival fall takes vanilla damage ==")
    s1 = scenario_pure_survival(host, port)
    print("== scenario 2: creative fall carries no damage after "
          "mid-air switch ==")
    s2 = scenario_creative_midair_switch(host, port)
    print(f"results: pure_survival={'OK' if s1 else 'FAIL'} "
          f"creative_switch={'OK' if s2 else 'FAIL'}")
    return 0 if (s1 and s2) else 1


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="CurseStone fall damage E2E check")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port))
