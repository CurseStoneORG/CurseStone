#!/usr/bin/env python3
"""End-to-end fire + particle check for a local CurseStone server.

Needs the FireTest plugin (stone platform at 140..144 / y70) and an
ops.txt entry for the test player. Sequence, all through the real
protocol-84 wire:

  1. place fire with flint & steel (creative)     -> UPDATE_BLOCK(51)
  2. punch the block UNDER the fire (survival,
     ACTION_START_BREAK, the 0.15 raycast rule)   -> UPDATE_BLOCK(0)
  3. stand inside the fire (survival)             -> SetEntityData flags
                                                     byte with ON_FIRE bit
  4. leave the fire                               -> ON_FIRE bit clears
  5. full bow draw + release                      -> arrow + critical
                                                     particle events
  6. short draw (control)                         -> arrow, NO crit events

Usage:
    python3 tools/fire_particles_check.py [--host 127.0.0.1] [--port 19132]
"""
import argparse
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from portal_travel_check import PortalClient  # noqa: E402
from linux_login_check import STAGE_SPAWN, STAGE_CHUNKS  # noqa: E402

MOVE_PLAYER = 0x10
TEXT_PACKET = 0x07
ADD_ENTITY = 0x0B
REMOVE_ENTITY = 0x0C
MOVE_ENTITY = 0x0F
UPDATE_BLOCK = 0x13
LEVEL_EVENT = 0x16
ENTITY_EVENT = 0x18
SET_ENTITY_DATA = 0x22
USE_ITEM = 0x1F
PLAYER_ACTION = 0x20

EVENT_SOUND_SHOOT = 1002
EVENT_PARTICLE_CRITICAL = 0x4000 | 2   # 0x4002
PROP_FLAGS_TYPE_BYTE_KEY = 0x00        # (TYPE_BYTE << 5) | index 0

ON_FIRE_BIT = 0x01

FIRE_X, FIRE_Y, FIRE_Z = 142, 71, 142   # fire cell built by FireTest
UNDER_Y = FIRE_Y - 1                    # stone below the fire cell
STAND = (FIRE_X + 0.5, FIRE_Y + 1.62, FIRE_Z + 2.5)   # 2 blocks south
INSIDE = (FIRE_X + 0.5, FIRE_Y + 1.62, FIRE_Z + 0.5)  # inside the fire cell


class FireClient(PortalClient):
    """PortalClient + fire/particle wire observers."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.block_updates = []     # (x, y, z, blockId)
        self.flag_values = []       # (flags_byte, monotonic counter)
        self.crit_events = 0
        self.shoot_sounds = 0
        self.arrows_spawned = 0
        self.arrow_eid = None
        self.arrow_track = []
        self.arrow_removed = 0
        self._flag_seq = 0

    def classify(self, payload: bytes):
        if not payload:
            return
        if payload[0] == UPDATE_BLOCK and len(payload) >= 11:
            x, z = struct.unpack(">ii", payload[1:9])
            y = payload[9]
            self.block_updates.append((x, y, z, payload[10]))
        elif payload[0] == SET_ENTITY_DATA and len(payload) >= 11:
            eid = struct.unpack(">q", payload[1:9])[0]
            off = 9
            while off < len(payload):
                key = payload[off]
                if key == 0x7F:
                    break
                vtype = (key >> 5) & 0x07
                idx = key & 0x1F
                if vtype == 0:                      # BYTE
                    if idx == 0:
                        self._flag_seq += 1
                        self.flag_values.append((payload[off + 1],
                                                 self._flag_seq))
                    off += 2
                elif vtype == 1:                    # SHORT
                    off += 3
                elif vtype in (2, 3):               # INT / FLOAT
                    off += 5
                elif vtype == 7:                    # LONG
                    off += 9
                elif vtype == 4:                    # STRING
                    slen = struct.unpack(">H", payload[off + 1:off + 3])[0]
                    off += 3 + slen
                elif vtype == 6:                    # POS
                    off += 13
                else:
                    break
        elif payload[0] == LEVEL_EVENT and len(payload) >= 19:
            evid = struct.unpack(">h", payload[1:3])[0]
            if evid == EVENT_PARTICLE_CRITICAL:
                self.crit_events += 1
            elif evid == EVENT_SOUND_SHOOT:
                self.shoot_sounds += 1
        elif payload[0] == ADD_ENTITY and len(payload) >= 13:
            etype = struct.unpack(">i", payload[9:13])[0]
            if etype == 80:
                self.arrows_spawned += 1
                self.arrow_eid = struct.unpack(">q", payload[1:9])[0]
        elif payload[0] == MOVE_ENTITY and len(payload) >= 24:
            eid = struct.unpack(">q", payload[1:9])[0]
            if eid == self.arrow_eid:
                x, y, z = struct.unpack(">fff", payload[9:21])
                self.arrow_track.append((round(x, 1), round(y, 1),
                                         round(z, 1)))
        elif payload[0] == REMOVE_ENTITY and len(payload) >= 9:
            if struct.unpack(">q", payload[1:9])[0] == self.arrow_eid:
                self.arrow_removed += 1
        PortalClient.classify(self, payload)

    # -- helpers --
    def player_action(self, action, x, y, z, face):
        pa = bytes([PLAYER_ACTION])
        pa += struct.pack(">q", 1)               # eid (server re-stamps it)
        pa += struct.pack(">i", action)
        pa += struct.pack(">iii", x, y, z)
        pa += struct.pack(">i", face)
        self.send_game(pa)

    def use_item_air(self, item_id):
        ui = bytes([USE_ITEM])
        ui += struct.pack(">iii", 0, 0, 0)       # no block clicked
        ui += bytes([0xFF])                      # face = air use
        ui += struct.pack(">fff", 0.0, 0.0, 0.0)
        ui += struct.pack(">fff", STAND[0], STAND[1], STAND[2])
        ui += struct.pack(">i", 0)               # slot
        ui += struct.pack(">h", item_id)
        ui += bytes([1])                         # count
        ui += struct.pack(">h", 0)               # damage
        ui += struct.pack("<H", 0)               # no nbt
        self.send_game(ui)

    def aim(self, yaw, pitch):
        # Wire rotation order: yaw, bodyYaw, pitch (MovePlayerPacket).
        mp = bytes([MOVE_PLAYER]) + struct.pack(">q", 1)
        mp += struct.pack(">fff", STAND[0], STAND[1], STAND[2])
        mp += struct.pack(">fff", yaw, yaw, pitch)
        mp += bytes([0, 1])
        self.send_game(mp)

    def last_update_at(self, x, y, z):
        for (bx, by, bz, bid) in reversed(self.block_updates):
            if (bx, by, bz) == (x, y, z):
                return bid
        return None


def fire_updates(c, x, y, z):
    return [b for b in c.block_updates if b[:3] == (x, y, z)]


def place_fire(c):
    """Creative: put flint & steel in hand and light the fire cell."""
    c.creative_take(0, 259)                      # flint & steel
    time.sleep(0.5)
    c.use_item_on(FIRE_X, UNDER_Y, FIRE_Z, 1,
                  STAND[0], STAND[1], STAND[2], 259)
    time.sleep(0.8)
    c.watch(1.2)


def fire_login(host, port, name):
    for attempt in range(3):
        c = FireClient(host, port, name)
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
        c.start_rx()
        c.watch(1.0)
        return c
    print(f"FAIL[{name}]: could not complete login")
    return None


def run(host: str, port: int) -> int:
    name = "FireOne"
    c = fire_login(host, port, name)
    if not c:
        return 1
    print(f"[{name}] login OK; building scene (survival)...")

    c.say("/gamemode 0")
    time.sleep(0.8)
    c.say(f"/teleport {STAND[0]} {FIRE_Y} {STAND[2]}")
    time.sleep(1.0)
    c.watch(1.0)
    c.say("/firebuild")       # platform build once the chunks are loaded
    time.sleep(0.8)
    c.watch(0.5)
    c.move(*STAND)
    time.sleep(0.3)

    failures = []

    # ---- 1. fire placement reaches the wire ----
    print(f"[{name}] scenario 1: flint & steel places fire (creative)...")
    c.say("/gamemode 1")
    time.sleep(0.8)
    place_fire(c)
    placed = fire_updates(c, FIRE_X, FIRE_Y, FIRE_Z)
    print(f"[{name}] fire cell updates: {placed}")
    if not placed or placed[-1][3] != 51:
        failures.append("placement UPDATE_BLOCK(51) not seen")

    # ---- 2. survival punch under the fire extinguishes it ----
    print(f"[{name}] scenario 2: survival punch under the fire...")
    c.say("/gamemode 0")
    time.sleep(0.8)
    n_before = len(c.block_updates)
    c.player_action(0, FIRE_X, UNDER_Y, FIRE_Z, 1)   # START_BREAK, face up
    c.watch(2.0)
    after = fire_updates(c, FIRE_X, FIRE_Y, FIRE_Z)[n_before:]
    print(f"[{name}] updates after punch: {after}")
    if not after or after[-1][3] != 0:
        failures.append("START_BREAK under fire did not broadcast air")

    # ---- 3. standing in fire sets the ON_FIRE flag ----
    print(f"[{name}] scenario 3: burning player carries the ON_FIRE flag...")
    c.say("/gamemode 1")
    time.sleep(0.8)
    place_fire(c)
    if not fire_updates(c, FIRE_X, FIRE_Y, FIRE_Z) or \
            fire_updates(c, FIRE_X, FIRE_Y, FIRE_Z)[-1][3] != 51:
        failures.append("re-placement UPDATE_BLOCK(51) not seen")
    c.say("/gamemode 0")
    time.sleep(0.8)
    c.move(*INSIDE)          # feet inside the fire cell
    c.watch(2.5)
    burning = [f for f in c.flag_values if f[0] & ON_FIRE_BIT]
    print(f"[{name}] flag bytes: {[f[0] for f in c.flag_values][-8:]}")
    if not burning:
        failures.append("no flags byte with ON_FIRE bit while burning")

    # ---- 4. leaving the fire clears the flag (burn timer runs out) ----
    print(f"[{name}] scenario 4: burn ends -> ON_FIRE bit clears...")
    c.move(*STAND)           # out of the fire
    c.say("/gamemode 1")     # stop the residual fire-tick damage
    time.sleep(0.8)
    set_seq = burning[-1][1] if burning else 0
    deadline = time.time() + 12.0
    cleared = False
    while time.time() < deadline:
        c.watch(1.0)
        after_set = [f for f in c.flag_values if f[1] > set_seq]
        if after_set and not (after_set[-1][0] & ON_FIRE_BIT):
            cleared = True
            break
    print(f"[{name}] flag bytes after leaving: "
          f"{[f[0] for f in c.flag_values][-8:]}")
    if not cleared:
        failures.append("ON_FIRE bit never cleared after leaving the fire")

    # ---- 5. full bow draw emits critical particles along the flight ----
    print(f"[{name}] scenario 5: full draw -> crit particles on flight...")
    c.creative_take(0, 261)                      # bow
    time.sleep(0.5)
    c.aim(180.0, -75.0)                          # north, near-vertical:
    time.sleep(0.2)                              # long flight over terrain
    crit_before = c.crit_events
    c.use_item_air(261)                          # start drawing
    time.sleep(1.3)                              # ~26 ticks (full charge)
    c.player_action(5, 0, 0, 0, 0)               # ACTION_RELEASE_ITEM
    c.watch(3.5)
    c.watch(2.0)   # let the full-charge arrow land before the control shot
    full_crits = c.crit_events - crit_before
    print(f"[{name}] arrows={c.arrows_spawned} shoot_sounds={c.shoot_sounds} "
          f"crit_events={full_crits}")
    print(f"[{name}] arrow path ({len(c.arrow_track)} moves, "
          f"removed={c.arrow_removed}): {c.arrow_track[:12]}")
    if c.arrows_spawned < 1:
        failures.append("no arrow entity spawned on release")
    if c.shoot_sounds < 1:
        failures.append("no shoot sound event")
    if full_crits < 5:
        failures.append(f"too few critical particles on full draw "
                        f"({full_crits})")

    # ---- 6. short draw: arrow flies but stays crit-free ----
    print(f"[{name}] scenario 6: short draw -> no crit particles...")
    c.aim(180.0, -75.0)
    time.sleep(0.2)
    crit_before = c.crit_events
    c.use_item_air(261)
    time.sleep(0.35)                             # ~7 ticks (weak shot)
    c.player_action(5, 0, 0, 0, 0)
    c.watch(3.0)
    short_crits = c.crit_events - crit_before
    print(f"[{name}] crit_events after short draw: {short_crits}")
    if short_crits != 0:
        failures.append(f"short draw emitted crit particles ({short_crits})")

    print(f"[{name}] result: "
          f"{'OK' if not failures else 'FAIL: ' + '; '.join(failures)}")
    return 0 if not failures else 2


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="CurseStone fire/particles E2E")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port))
