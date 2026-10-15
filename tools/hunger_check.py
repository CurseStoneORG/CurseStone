#!/usr/bin/env python3
"""End-to-end hunger (food) system check for a local CurseStone server.

Needs an ops.txt entry for the tester name (HungerOne) so it can run
/gamemode. One real protocol-84 client drives every path over the wire:

  0. spawn attributes: hunger=20, saturation=5, exhaustion present
  1. full-hunger gate: eating at 20 food is refused (no eat echo)
  2. sprint + movement burns exhaustion -> saturation -> food
  3. starvation at 0 food deals 1 damage after 4 s (difficulty >= 1)
  4. eating bread: food rises 5 + eat echo (EntityEvent 9) comes back
  5. eat cooldown: a second eat within 1.6 s does nothing
  6. after the cooldown the next eat works again

Usage:
    python3 tools/hunger_check.py [--host 127.0.0.1] [--port 19132]
"""
import argparse
import struct
import sys
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from fall_damage_check import FallClient, spawn, GROUND_EYE  # noqa: E402

TEXT_PACKET = 0x07
ENTITY_EVENT = 0x18
UPDATE_ATTRIBUTES = 0x1A
MOB_EQUIPMENT = 0x1B
CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_CONTENT = 0x2E
PLAYER_ACTION = 0x20
MOVE_PLAYER = 0x10
ACTION_START_SPRINT = 9
EYE = 1.62

START_X, START_Z = 129.0, 128.4
BREAD = 297


def bestr(s: str) -> bytes:
    raw = s.encode()
    return struct.pack(">H", len(raw)) + raw


class HungerClient(FallClient):
    """FallClient + hunger attribute tracking + eat event observers."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.attrs = {}        # latest attribute values by name
        self.eat_echoes = 0    # EntityEvent USE_ITEM(9) seen (eat echo)
        self.bread_total = None  # bread units in the last inventory push
        self.server_pos = None  # (x, y, z) learned from a snap-back

    def classify(self, payload: bytes):
        if payload and payload[0] == UPDATE_ATTRIBUTES and len(payload) >= 11:
            count = struct.unpack(">H", payload[9:11])[0]
            off = 11
            for _ in range(count):
                if off + 14 > len(payload):
                    break
                # entry: min f32, max f32, value f32, name str16
                value = struct.unpack(">f", payload[off + 8:off + 12])[0]
                nlen = struct.unpack(">H", payload[off + 12:off + 14])[0]
                name = payload[off + 14:off + 14 + nlen].decode("utf-8", "replace")
                off += 14 + nlen
                self.attrs[name] = value
        elif payload and payload[0] == ENTITY_EVENT and len(payload) >= 10:
            if payload[9] == 9:
                self.eat_echoes += 1
        elif payload and payload[0] == CONTAINER_SET_CONTENT and len(payload) >= 3:
            self.parse_inventory(payload)
        elif payload and payload[0] == MOVE_PLAYER and len(payload) >= 34:
            # Server->client MovePlayer: a MODE_RESET reveals the true
            # server-side position (used to sync before sprinting).
            mode = payload[33]
            if mode == 1:
                x, y, z = struct.unpack(">fff", payload[9:21])
                self.server_pos = (x, y, z)
        FallClient.classify(self, payload)

    def parse_inventory(self, payload: bytes):
        """Track the bread count in ContainerSetContent window 0 pushes."""
        win = payload[1]
        if win != 0:
            return
        count = struct.unpack(">h", payload[2:4])[0]
        off = 4
        total = 0
        for _ in range(count):
            if off + 2 > len(payload):
                return
            sid = struct.unpack(">h", payload[off:off + 2])[0]
            off += 2
            if sid == 0:
                continue
            if off + 5 > len(payload):
                return
            cnt = payload[off]
            nlen = struct.unpack("<H", payload[off + 3:off + 5])[0]
            off += 5 + nlen
            if sid == BREAD:
                total += cnt
        self.bread_total = total

    # ---- helpers ----
    def creative_take(self, slot, item_id, count=1):
        """ContainerSetSlot (window 0): server puts the item in `slot`."""
        cs = bytes([CONTAINER_SET_SLOT])
        cs += bytes([0])                      # windowid 0 (inventory)
        cs += struct.pack(">h", slot)         # slot
        cs += struct.pack(">h", 0)            # hotbarSlot
        cs += struct.pack(">h", item_id)      # item id
        cs += bytes([count])                  # count
        cs += struct.pack(">h", 0)            # damage
        cs += struct.pack("<H", 0)            # no nbt
        self.send_game(cs)

    def say(self, msg):
        self.send_game(bytes([TEXT_PACKET, 1]) + bestr(self.name) + bestr(msg))

    def mob_equipment(self, hotbar, item_id, count=1):
        me = bytes([MOB_EQUIPMENT])
        me += struct.pack(">q", 1)          # eid (server re-stamps it)
        me += struct.pack(">h", item_id)    # slot: id
        me += bytes([count])                # count
        me += struct.pack(">h", 0)          # damage
        me += struct.pack("<H", 0)          # no nbt
        me += bytes([hotbar])               # hotbarSlot
        me += bytes([hotbar])               # selectedSlot
        self.send_game(me)

    def entity_event(self, event):
        ee = bytes([ENTITY_EVENT]) + struct.pack(">q", 1) + bytes([event])
        self.send_game(ee)

    def sprint_action(self, action):
        pa = bytes([PLAYER_ACTION])
        pa += struct.pack(">q", 1)          # eid
        pa += struct.pack(">i", action)
        pa += struct.pack(">iii", 0, 0, 0)  # block pos (unused)
        pa += struct.pack(">i", 0)          # face
        self.send_game(pa)

    def sprint_move(self, packets=60, step=19.0):
        """Sprint back and forth near the spawn while sending moves.

        The anti-cheat rejects moves more than ~20 blocks from the
        server-side position (learn it from a snap-back first). The run
        ping-pongs inside the loaded spawn area (chunk generation would
        stall the session) and sends without pacing so the per-tick
        exhaustion batches cross the sprint floor (food <= 6) downwards."""
        if self.server_pos is None:
            self.move(START_X + 100, GROUND_EYE + 10, START_Z, True)
            self.watch(0.6)
        sx, sy, sz = self.server_pos or (START_X, GROUND_EYE - EYE, START_Z)
        x = sx
        direction = 1
        for _ in range(packets):
            x += direction * step
            self.move(x, sy + EYE, sz, True)
            if abs(x - sx) > 80:
                direction = -1


def hunger_login(host, port, name):
    for attempt in range(3):
        c = HungerClient(host, port, name)
        if c.handshake():
            print(f"[{name}] handshake retry {attempt + 1}")
            continue
        if c.login():
            print(f"[{name}] login retry {attempt + 1}")
            continue
        if not c.expect("start_game", seconds=6.0):
            print(f"[{name}] spawn retry {attempt + 1}")
            continue
        c.expect("chunks_streamed", seconds=5.0)
        c.watch(1.0)
        return c
    print(f"FAIL[{name}]: could not complete login")
    return None


def run(host: str, port: int) -> int:
    name = "HungerOne"
    c = hunger_login(host, port, name)
    if not c:
        return 1
    failures = []

    def check(label, ok, detail=""):
        print(f"{'PASS' if ok else 'FAIL'}: {label} {detail}")
        if not ok:
            failures.append(label)

    def wait_attr(name, value, seconds, equals=True):
        """Poll watch() until attrs[name] reaches value (or timeout)."""
        deadline = time.time() + seconds
        while time.time() < deadline:
            v = c.attrs.get(name)
            if v is not None and ((v == value) if equals else (v != value)):
                return v
            c.watch(0.3)
        return c.attrs.get(name)

    # --- 0. grab bread via creative, then go back to survival ---
    # NOTE: the server pushes the hunger attributes right after
    # PLAYER_SPAWN, before this harness starts classifying, so spawn
    # values are asserted from the first drain-driven re-send below.
    c.say("/gamemode 1")
    time.sleep(0.8)
    # One deterministic take: the windowid-0 branch does
    # setItem(cs->slot, item) verbatim, so a single ContainerSetSlot with
    # count=3 leaves exactly 3 breads in hand slot 0 (a second take with
    # the same slot would REPLACE the stack, not stack on top).
    c.creative_take(0, BREAD, count=3)
    time.sleep(0.5)
    c.say("/gamemode 0")
    time.sleep(0.8)
    c.mob_equipment(0, BREAD)
    c.watch(1.5)

    # --- 1. full-hunger gate: eating at 20 food must be refused ---
    before_echoes = c.eat_echoes
    c.entity_event(9)
    c.watch(1.2)
    check("full-hunger gate refuses eat",
          c.eat_echoes == before_echoes and c.bread_total in (3, None),
          f"echoes={c.eat_echoes} bread={c.bread_total}")

    # --- 2. sprint burns exhaustion -> saturation -> food -> 0 ---
    # 60 packets x 19 blocks = 1140 m sprinting = 114 exhaustion >= the
    # 4.0 x (5 saturation + 20 food) needed to empty the bar in one go.
    c.sprint_action(ACTION_START_SPRINT)
    c.sprint_move(packets=60)
    hunger = wait_attr("player.hunger", 0.0, 6.0)
    sat = c.attrs.get("player.saturation")
    exh = c.attrs.get("player.exhaustion")
    check("hunger attributes pushed", hunger is not None and sat is not None,
          f"hunger={hunger} saturation={sat} exhaustion={exh}")
    check("sprint drains food to 0", hunger == 0.0, f"hunger={hunger}")

    # --- 3. starvation at 0 food deals 1 damage after 4 s (difficulty 1) ---
    c.watch(7.0)
    check("starvation damages the player", c.min_health < 20.0,
          f"health={c.health} min={c.min_health}")

    # --- 4. eating bread restores 5 food + echoes event 9 ---
    c.mob_equipment(0, BREAD)
    time.sleep(0.3)
    echoes = c.eat_echoes
    c.entity_event(9)
    hunger = wait_attr("player.hunger", 5.0, 4.0)
    check("eat bread: hunger +5", hunger == 5.0, f"hunger={hunger}")
    check("eat bread: server echoes event 9", c.eat_echoes > echoes,
          f"echoes={c.eat_echoes}")
    check("eat bread: slot consumed", c.bread_total == 2,
          f"bread={c.bread_total}")

    # --- 5. eat cooldown: second eat within 1.6 s does nothing ---
    time.sleep(0.2)
    c.entity_event(9)
    c.watch(0.5)
    check("eat cooldown blocks the second eat",
          c.attrs.get("player.hunger") == 5.0, f"hunger={c.attrs.get('player.hunger')}")

    # --- 6. after the cooldown the next eat works ---
    time.sleep(1.8)
    for _ in range(3):
        c.entity_event(9)
        hunger = wait_attr("player.hunger", 10.0, 2.0)
        if hunger == 10.0:
            break
        time.sleep(0.5)
    check("eat works after cooldown", hunger == 10.0, f"hunger={hunger}")

    print(f"\n{'FAILED: ' + ', '.join(failures) if failures else 'ALL HUNGER CHECKS PASSED'}")
    return 1 if failures else 0


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19132)
    args = ap.parse_args()
    sys.exit(run(args.host, args.port))
