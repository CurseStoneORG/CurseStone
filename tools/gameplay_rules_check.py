#!/usr/bin/env python3
"""End-to-end gameplay rules check for a local CurseStone server.

Spawns its own runtime (flat world, ops for the tester) and drives the
real RakNet + protocol-84 path to prove the death/bed/milk rules:

  phase A (keep-inventory: false, bed-spawn: true)
    A1. place bed + tap by day   -> SetSpawnPosition binds to the bed,
                                    night still refuses the sleep
    A2. sleep at night           -> night skipped, spawn still bound
    A3. swiftness + milk         -> MobEffect REMOVE packets + movement
                                    speed back to base + empty bucket
    A4. /kill with items         -> items dropped as entities, respawn
                                    happens at the bed
    A5. relog                    -> inventory is NOT persisted
    A6. break the bed            -> SetSpawnPosition back to the world
                                    spawn; respawn happens there
  phase B (keep-inventory: true, bed-spawn: false)
    B1. tap bed                  -> spawn is NOT bound (rule off)
    B2. /kill with items         -> no drops
    B3. relog                    -> inventory persisted (kept)

Usage:
    python3 tools/gameplay_rules_check.py [--server /path/to/CurseStone]
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from fall_damage_check import spawn  # noqa: E402
from hunger_check import HungerClient  # noqa: E402

SET_TIME_PACKET = 0x08
REMOVE_BLOCK_PACKET = 0x12
UPDATE_BLOCK_PACKET = 0x13
LEVEL_EVENT_PACKET = 0x16
MOB_EFFECT = 0x19
USE_ITEM_PACKET = 0x1F
SET_SPAWN_POSITION_PACKET = 0x26

TEXT_PACKET = 0x07
CONTAINER_SET_SLOT = 0x2C
MOB_EQUIPMENT = 0x1B

ITEM_BED = 355
ITEM_DIAMOND = 264
ITEM_POTION = 373
# Milk reaches the server as BUCKET(325) aux/meta 1 (the 0.15.10 creative
# catalog entry) — the standalone 335 id is unreachable on the wire.
ITEM_MILK = 325
MILK_META = 1
META_SWIFTNESS = 14

WORLD_SPAWN = (128, 4, 128)
BED_FOOT = (132, 4, 132)   # placed cell; head is an adjacent cell at y=4


class RulesClient(HungerClient):
    """HungerClient + bed/time/spawn/effect observers and block taps."""

    def __init__(self, host, port, name):
        super().__init__(host, port, name)
        self.spawn_pks = []      # every SetSpawnPositionPacket (x, y, z)
        self.time_pks = []       # every SetTimePacket value
        self.block_ups = []      # UpdateBlock events (x, y, z, id, meta)
        self.effect_pkts = []    # (eventId, effectId) of MobEffectPacket

    def classify(self, payload: bytes):
        if payload and payload[0] == 0x09 and len(payload) >= 34:
            # START_GAME: ... [eid i64][spawnX i32][spawnY i32][spawnZ i32]
            self.spawn = struct.unpack(">iii", payload[22:34])
        if payload and payload[0] == SET_SPAWN_POSITION_PACKET and len(payload) >= 13:
            sx, sy, sz = struct.unpack(">iii", payload[1:13])
            self.spawn_pks.append((sx, sy, sz))
        elif payload and payload[0] == SET_TIME_PACKET and len(payload) >= 5:
            self.time_pks.append(struct.unpack(">i", payload[1:5])[0])
        elif payload and payload[0] == UPDATE_BLOCK_PACKET and len(payload) >= 12:
            x, z = struct.unpack(">ii", payload[1:9])
            self.block_ups.append((x, payload[9], z, payload[10], payload[11] & 0x0F))
        elif payload and payload[0] == LEVEL_EVENT_PACKET and len(payload) >= 19:
            pass  # sounds/particles: not needed here
        elif payload and payload[0] == MOB_EFFECT and len(payload) >= 13:
            self.effect_pkts.append((payload[9], payload[10]))
        else:
            super().classify(payload)

    # ---- game packet builders (same wire format as door_bed_flow) ----
    @staticmethod
    def slot_bytes(item_id: int, count: int = 1, damage: int = 0) -> bytes:
        if item_id <= 0:
            return struct.pack(">h", 0)
        return (struct.pack(">h", item_id) + bytes([count]) +
                struct.pack(">h", damage) + struct.pack("<H", 0))

    def use_item(self, x, y, z, face, item_id=0, damage=0):
        body = (bytes([USE_ITEM_PACKET]) +
                struct.pack(">iii", x, y, z) + bytes([face]) +
                struct.pack(">fff", 0.5, 1.0, 0.5) +
                struct.pack(">fff", x + 0.5, y + 1.0, z + 0.5) +
                struct.pack(">i", 0) +
                self.slot_bytes(item_id, 1, damage))
        self.send_game(body)

    def remove_block(self, x, y, z):
        body = (bytes([REMOVE_BLOCK_PACKET]) +
                struct.pack(">q", 1) +               # eid (server re-stamps it)
                struct.pack(">i", x) + struct.pack(">i", z) + bytes([y]))
        self.send_game(body)

    # ---- meta-aware creative helpers (potion_check flavour) ----
    def creative_take(self, slot, item_id, count=1, meta=0):
        cs = bytes([CONTAINER_SET_SLOT])
        cs += bytes([0])                      # windowid 0 (inventory)
        cs += struct.pack(">h", slot)         # slot
        cs += struct.pack(">h", 0)            # hotbarSlot
        cs += struct.pack(">h", item_id)      # item id
        cs += bytes([count])                  # count
        cs += struct.pack(">h", meta)         # damage/meta
        cs += struct.pack("<H", 0)            # no nbt
        self.send_game(cs)

    def mob_equipment(self, hotbar, item_id, meta=0):
        me = bytes([MOB_EQUIPMENT])
        me += struct.pack(">q", 1)            # eid (server re-stamps it)
        me += struct.pack(">h", item_id)      # slot: id
        me += bytes([1])                      # count
        me += struct.pack(">h", meta)         # damage/meta
        me += struct.pack("<H", 0)            # no nbt
        me += bytes([hotbar + 9])                 # hotbarSlot
        me += bytes([hotbar])                 # selectedSlot
        self.send_game(me)

    def stock_items(self, items):
        """Creative-stock (slot, id, count, meta) tuples with retry."""
        for _ in range(3):
            self.say("/gamemode 1")
            time.sleep(0.8)
            for entry in items:
                slot, item_id, count = entry[0], entry[1], entry[2]
                meta = entry[3] if len(entry) > 3 else 0
                self.mob_equipment(slot, item_id, meta)
                time.sleep(0.1)
                self.creative_take(slot, item_id, count, meta)
                time.sleep(0.15)
            time.sleep(0.4)
            self.say("/gamemode 0")
            time.sleep(0.8)
            self.watch(0.5)
            want = {}
            for entry in items:
                want[entry[1]] = want.get(entry[1], 0) + entry[2]
            if all(self.inv_counts.get(k, 0) >= v for k, v in want.items()):
                return True
        return False

    def drink(self, slot, item_id, meta=0, wait_after=1.0):
        """Equip the stocked drinkable and finish consuming it."""
        self.mob_equipment(slot, item_id, meta)
        time.sleep(0.3)
        since = len(self.effect_pkts)
        self.entity_event(9)
        self.watch(wait_after)
        return since


def boot_server(server_bin, runtime, port, keep_inventory, bed_spawn):
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=GameplayRules\nview-distance=4\ngamemode=0\n"
                "difficulty=1\n" % port)
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "gameplay:\n  keep-inventory: %s\n  bed-spawn: %s\n"
                % ("true" if keep_inventory else "false",
                   "true" if bed_spawn else "false"))
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("RulesOne\n")
    # Truncate the log so wait_boot() cannot see the previous boot.
    open(os.path.join(runtime, "server.log"), "w").close()
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(server_bin)
    return subprocess.Popen([server_bin], cwd=runtime, env=env,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)


def wait_boot(proc, runtime, timeout=40):
    server_log = os.path.join(runtime, "server.log")
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            return False
        try:
            with open(server_log) as f:
                if "Server ready" in f.read():
                    return True
        except FileNotFoundError:
            pass
        time.sleep(0.4)
    return False


def console(proc, cmd):
    try:
        proc.stdin.write((cmd + "\n").encode())
        proc.stdin.flush()
    except BrokenPipeError:
        pass


def stop_server(proc):
    """Graceful console stop first; SIGKILL fallback (SIGTERM hangs)."""
    try:
        console(proc, "stop")
        proc.wait(timeout=20)
    except subprocess.TimeoutExpired:
        proc.kill()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            pass
    time.sleep(1.0)


def kill_and_respawn(c, timeout=10.0):
    """/kill ourselves and take the respawn; returns the respawn pos."""
    c.say("/gamemode 0")
    time.sleep(0.6)
    c.watch(0.4)
    c.say("/kill %s" % c.name)
    deadline = time.time() + 5.0
    while time.time() < deadline:
        c.watch(0.4)
        if c.health <= 1.0:
            break
    c.health = 0.0
    c.player_action(7)  # ACTION_SPAWN_SAME_DIMENSION
    deadline = time.time() + timeout
    while time.time() < deadline:
        c.watch(0.5)
        if c.health >= 19.0:
            return c.server_pos
    return None


def relog(host, port, name):
    """Close the session and log back in; the kick save persists state."""
    time.sleep(2.2)
    c2 = RulesClient(host, port, name)
    if not spawn(c2, name):
        return None
    c2.drain(2.0)
    return c2


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone gameplay rules check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--port", type=int, default=19139)
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print("FAIL: server binary not found: %s" % args.server)
        return 1

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    runtime = tempfile.mkdtemp(prefix="cs_rules_")
    proc = boot_server(args.server, runtime, args.port,
                       keep_inventory=False, bed_spawn=True)
    try:
        if not wait_boot(proc, runtime):
            print("FAIL: server never booted")
            return 1

        # ================= phase A =================
        c = RulesClient("127.0.0.1", args.port, "RulesOne")
        if not spawn(c, "RulesOne"):
            print("FAIL: login")
            return 1
        c.drain(2.0)
        c.player_action(7)
        c.drain(1.0)
        if not c.reset_state():
            check(False, "deterministic start (kill + respawn)")
            return 1
        check(True, "deterministic start (kill + respawn)")
        world_spawn = c.spawn

        # ---- A1. bed place + day tap binds the spawn ----
        ok_stock = c.stock_items([(0, ITEM_BED, 1), (1, ITEM_DIAMOND, 5)])
        check(ok_stock, "stock bed + 5 diamonds")
        # Real-client parity: the server consumes from the held slot, so
        # the bed must be equipped (not just named in the packet).
        c.mob_equipment(0, ITEM_BED, 0)
        time.sleep(0.2)
        c.use_item(BED_FOOT[0], 3, BED_FOOT[2], 1, item_id=ITEM_BED)
        c.drain(1.2)
        ups = [e for e in c.block_ups if e[3] == 26 and e[1] == 4]
        foot_ups = [e for e in ups if e[0] == BED_FOOT[0] and e[2] == BED_FOOT[2]
                    and (e[4] & 0x08) == 0]
        head_ups = [e for e in ups if not (e[0] == BED_FOOT[0] and e[2] == BED_FOOT[2])
                    and (e[4] & 0x08) == 0x08]
        check(len(foot_ups) >= 1, "bed foot placed (26, no 0x08)")
        check(len(head_ups) >= 1, "bed head placed (26, 0x08 set)")
        if not head_ups:
            print("FAILED: " + ", ".join(failures))
            return 1
        head = (head_ups[0][0], head_ups[0][1], head_ups[0][2])

        console(proc, "time set 1000")
        c.drain(1.0)
        c.spawn_pks.clear()
        c.time_pks.clear()
        c.use_item(BED_FOOT[0], 4, BED_FOOT[2], 1, item_id=0)
        c.drain(1.2)
        day_bind = [e for e in c.spawn_pks if e[1] == 4 and e[0] == head[0]
                    and e[2] == head[2]]
        check(len(day_bind) >= 1, "day tap binds the spawn to the bed head")
        check(len(c.time_pks) == 0, "day tap does not skip the night")

        # ---- A2. night sleep still works ----
        console(proc, "time set 15000")
        deadline = time.time() + 4.0
        while time.time() < deadline:
            c.drain(0.3)
            if 15000 in c.time_pks:
                break
        check(15000 in c.time_pks, "`time set 15000` received")
        c.spawn_pks.clear()
        c.time_pks.clear()
        c.use_item(BED_FOOT[0], 4, BED_FOOT[2], 1, item_id=0)
        bound = None
        deadline = time.time() + 4.0
        while time.time() < deadline:
            c.drain(0.3)
            bound = [e for e in c.spawn_pks if e[0] == head[0] and e[1] == head[1]
                     and e[2] == head[2]]
            if bound:
                break
        check(len(bound) >= 1, "sleep keeps the spawn bound to the bed")
        morning = None
        deadline = time.time() + 8.0
        while time.time() < deadline:
            c.drain(0.4)
            if any(t != 15000 for t in c.time_pks):
                morning = True
                break
        check(morning is not None, "sleep skips the night")

        # ---- A3. milk clears every effect ----
        ok_stock = c.stock_items([(2, ITEM_POTION, 1, META_SWIFTNESS),
                                  (3, ITEM_MILK, 1, MILK_META)])
        check(ok_stock, "stock swiftness potion + milk bucket")
        c.drink(2, ITEM_POTION, META_SWIFTNESS)
        speed = None
        deadline = time.time() + 4.0
        while time.time() < deadline:
            c.drain(0.3)
            speed = c.attrs.get("generic.movementSpeed")
            if speed is not None and abs(speed - 0.12) < 0.005:
                break
        check(speed is not None and abs(speed - 0.12) < 0.005,
              "swiftness raises movementSpeed to 0.12 (got %s)" % speed)
        time.sleep(1.7)  # drink cooldown
        slots_before = len(c.slot_writes)
        since = c.drink(3, ITEM_MILK, MILK_META)
        removes = [p for p in c.effect_pkts[since:] if p[0] == 3]
        check(len(removes) >= 1, "milk sends MobEffect REMOVE (%d)" % len(removes))
        speed = None
        deadline = time.time() + 4.0
        while time.time() < deadline:
            c.drain(0.3)
            speed = c.attrs.get("generic.movementSpeed")
            if speed is not None and abs(speed - 0.1) < 0.005:
                break
        check(speed is not None and abs(speed - 0.1) < 0.005,
              "movementSpeed restored to base 0.1 (got %s)" % speed)
        bucket = any(iid == 325 and cnt >= 1 for _, iid, cnt in c.slot_writes[slots_before:])
        check(bucket, "milk leaves an empty bucket")

        # ---- A4. death drops the items and respawns at the bed ----
        drops_before = c.drops
        pos = kill_and_respawn(c)
        check(pos is not None, "kill + respawn completes")
        dropped = c.drops - drops_before
        check(dropped >= 1, "death drops the inventory (%d entities)" % dropped)
        near_bed = (pos is not None and abs(pos[0] - (head[0] + 0.5)) <= 2.0 and
                    abs(pos[2] - (head[2] + 0.5)) <= 2.0)
        check(near_bed, "respawn at the bed (pos=%s head=%s)" % (pos, head))

        # ---- A5. relog: the dropped inventory is not persisted ----
        c.sock.close()
        c2 = relog("127.0.0.1", args.port, "RulesOne")
        check(c2 is not None, "relog after keep-inventory death")
        if c2:
            check(c2.inv_counts.get(ITEM_DIAMOND, 0) == 0,
                  "inventory empty after drop death (diamonds=%s)"
                  % c2.inv_counts.get(ITEM_DIAMOND))

            # ---- A6. breaking the bed restores the world spawn ----
            c2.spawn_pks.clear()
            c2.remove_block(BED_FOOT[0], 4, BED_FOOT[2])
            c2.drain(1.5)
            restored = [e for e in c2.spawn_pks if tuple(e) == tuple(world_spawn)]
            check(len(restored) >= 1,
                  "bed break restores the world spawn point (%s)" % c2.spawn_pks[-2:])
            pos = kill_and_respawn(c2)
            check(pos is not None, "respawn after bed break completes")
            near_world = (pos is not None and
                          abs(pos[0] - (world_spawn[0] + 0.5)) <= 3.0 and
                          abs(pos[2] - (world_spawn[2] + 0.5)) <= 3.0)
            check(near_world, "respawn at the world spawn (pos={})".format(pos))
            c2.sock.close()
        else:
            check(False, "bed break restores the world spawn point")
            check(False, "respawn after bed break completes")

        # ================= phase B =================
        # Fresh runtime: the phase A world still has the dropped item
        # entities lying at the world spawn, which the respawned player
        # would pick up and skew the kept-inventory counts.
        stop_server(proc)
        runtime_b = tempfile.mkdtemp(prefix="cs_rules_b_")
        proc = boot_server(args.server, runtime_b, args.port,
                           keep_inventory=True, bed_spawn=False)
        if not wait_boot(proc, runtime_b):
            print("FAIL: phase B server never booted")
            return 1
        c3 = RulesClient("127.0.0.1", args.port, "RulesOne")
        if not spawn(c3, "RulesOne"):
            print("FAIL: phase B login")
            return 1
        c3.drain(2.0)
        c3.player_action(7)
        c3.drain(1.0)
        if not c3.reset_state():
            check(False, "phase B deterministic start")
            return 1
        check(True, "phase B deterministic start")

        ok_stock = c3.stock_items([(0, ITEM_BED, 1), (1, ITEM_DIAMOND, 5)])
        check(ok_stock, "phase B stock bed + 5 diamonds")
        c3.mob_equipment(0, ITEM_BED, 0)
        time.sleep(0.2)
        c3.use_item(BED_FOOT[0], 3, BED_FOOT[2], 1, item_id=ITEM_BED)
        c3.drain(1.2)
        c3.spawn_pks.clear()
        c3.use_item(BED_FOOT[0], 4, BED_FOOT[2], 1, item_id=0)
        c3.drain(1.2)
        check(len(c3.spawn_pks) == 0,
              "bed-spawn: false does not bind the spawn")

        drops_before = c3.drops
        pos = kill_and_respawn(c3)
        check(pos is not None, "phase B kill + respawn completes")
        dropped = c3.drops - drops_before
        check(dropped == 0, "keep-inventory: true drops nothing (%d)" % dropped)
        c3.sock.close()
        c4 = relog("127.0.0.1", args.port, "RulesOne")
        check(c4 is not None, "phase B relog")
        if c4:
            check(c4.inv_counts.get(ITEM_DIAMOND, 0) == 5,
                  "kept inventory persists (diamonds=%s)"
                  % c4.inv_counts.get(ITEM_DIAMOND))
            c4.sock.close()
        else:
            check(False, "kept inventory persists")
    finally:
        stop_server(proc)
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)
            try:
                shutil.rmtree(runtime_b, ignore_errors=True)
            except NameError:
                pass

    print("")
    if failures:
        print("FAILED: " + ", ".join(failures))
        return 1
    print("RESULT: ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
