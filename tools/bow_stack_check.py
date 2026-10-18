#!/usr/bin/env python3
"""End-to-end bow duplication / illegal-stack check.

One real protocol-84 client pair (MCPE 0.15.10) against a self-managed
flat server. Reproduces the reported sequence — creative bow take, two
bows, death, drop pickup, hand-off — and asserts the inventory never
enters an illegal state:

  Phase 1  one creative bow take yields exactly one bow (count=1).
  Phase 2  two bows die: two separate count=1 drop entities; picking
           them up lands them in two separate slots — never a fused
           count=2 slot (the fused slot is what made the bow invisible
           to viewers and unshootable for the holder).
  Phase 3  the picked-up bow is broadcast to the watcher with count=1
           (MobEquipmentPacket).
  Phase 4  a bow that went through the death/pickup cycle still fires
           (release spawns an arrow entity).
  Phase 5  a client reporting a legacy count=2 bow write gets it
           clamped; the overflow bow lands in its own slot.
  Phase 6  a named bow (slot NBT) survives a death round trip: the drop
           entity and the re-picked slot both carry the same NBT blob.

    python3 tools/bow_stack_check.py [--server BIN] [--port N]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from mp_bow_death_arrow_check import MpClient, drain_until  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

ADD_ITEM_ENTITY = 0x0D
MOB_EQUIPMENT = 0x1B
CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_CONTENT = 0x2E

BOW = 261
ARROW = 262


def display_nbt(name: str) -> bytes:
    """The little-endian network NBT compound the 0.15 slot format
    carries behind its u16 LE length — same layout as the server's
    buildDisplayNbt(): root compound {display:{Name:name}}."""
    def u16le(v):
        return struct.pack("<H", v)

    def tag_string(s):
        raw = s.encode()
        return u16le(len(raw)) + raw

    blob = bytes([0x0A]) + u16le(0)          # root compound, empty name
    blob += bytes([0x0A]) + tag_string("display")
    blob += bytes([0x08]) + tag_string("Name") + tag_string(name)
    blob += bytes([0x00])                    # end display
    blob += bytes([0x00])                    # end root
    return blob


class StackClient(MpClient):
    """MpClient + MobEquipment capture, NBT-aware drop/slot capture and
    per-slot window-0 content dumps."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.me_pkts = []        # (eid, id, count, meta, hotbar)
        self.drops_nbt = []      # (eid, id, count, meta, nbt)
        self.slot_writes_nbt = []  # (win, slot, id, meta, count, nbt)
        self.inv_slots = {}      # slot index -> (id, meta, count)

    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if pid == MOB_EQUIPMENT and len(p) >= 13:
            eid = struct.unpack(">q", p[1:9])[0]
            iid = struct.unpack(">h", p[9:11])[0]
            if iid > 0 and len(p) >= 15:
                cnt = p[11]
                meta = struct.unpack(">h", p[12:14])[0]
                nlen = struct.unpack("<H", p[14:16])[0] if len(p) >= 16 else 0
                hotbar = p[16 + nlen] if len(p) > 16 + nlen else 0
                self.me_pkts.append((eid, iid, cnt, meta, hotbar))
        elif pid == ADD_ITEM_ENTITY and len(p) >= 14:
            eid, iid = struct.unpack(">qh", p[1:11])
            if iid > 0:
                cnt = p[11]
                meta = struct.unpack(">h", p[12:14])[0]
                nbt = b""
                if len(p) >= 16:
                    nlen = struct.unpack("<H", p[14:16])[0]
                    if nlen and len(p) >= 16 + nlen:
                        nbt = p[16:16 + nlen]
                self.drops_nbt.append((eid, iid, cnt, meta, nbt))
        elif pid == CONTAINER_SET_SLOT and len(p) >= 13:
            win = p[1]
            slot = struct.unpack(">h", p[2:4])[0]
            sid = struct.unpack(">h", p[6:8])[0]
            if sid > 0:
                cnt = p[8]
                meta = struct.unpack(">h", p[9:11])[0]
                nbt = b""
                if len(p) >= 13:
                    nlen = struct.unpack("<H", p[11:13])[0]
                    if nlen and len(p) >= 13 + nlen:
                        nbt = p[13:13 + nlen]
                self.slot_writes_nbt.append((win, slot, sid, meta, cnt, nbt))
        elif pid == CONTAINER_SET_CONTENT and len(p) >= 4:
            self.parse_inv_slots(p)
        MpClient.on_game_packet(self, p)

    def parse_inv_slots(self, p: bytes):
        """window-0 ContainerSetContent: per-slot index dump (36 main
        slots + 9 padding, then the hotbar links)."""
        win = p[1]
        count = struct.unpack(">H", p[2:4])[0]
        off = 4
        slots = {}
        for idx in range(count):
            if off + 2 > len(p):
                return
            sid = struct.unpack(">h", p[off:off + 2])[0]
            off += 2
            if sid == 0:
                continue
            if off + 5 > len(p):
                return
            cnt = p[off]
            meta = struct.unpack(">h", p[off + 1:off + 3])[0]
            nlen = struct.unpack("<H", p[off + 3:off + 5])[0]
            off += 5 + nlen
            if idx < 36:
                slots[idx] = (sid, meta, cnt)
        if win == 0:
            self.inv_slots = slots

    def creative_take_nbt(self, slot, item_id, count=1, meta=0, nbt=b""):
        cs = bytes([CONTAINER_SET_SLOT, 0])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", 0)
        cs += struct.pack(">h", item_id)
        cs += bytes([count])
        cs += struct.pack(">h", meta)
        cs += struct.pack("<H", len(nbt)) + nbt
        self.send_game(cs)

    def drop_held(self):
        # DropItemPacket: type(one=false=0), itemid, count, meta,
        # then the head position triple. One whole stack per packet.
        body = (bytes([0x0F]) +
                bytes([0]) +
                struct.pack(">h", self.inv_held_id()) +
                bytes([1]) +
                struct.pack(">h", 0) +
                struct.pack(">fff", 0.0, 0.0, 0.0))
        self.send_game(body)

    def inv_held_id(self):
        for idx in sorted(self.inv_slots):
            sid, _, cnt = self.inv_slots[idx]
            if sid != 0 and cnt > 0:
                return sid
        return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone bow stack check")
    ap.add_argument("--server", default="/home/z/my-project/CurseStone/build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19146)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_bowstack_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=BowStack\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("StackOne\nStackTwo\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" + extra_ld).lstrip(":")

    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    def console(cmd: str):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

    try:
        deadline = time.time() + 40
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server process exited during boot")
                return 1
            try:
                with open(server_log) as f:
                    if "Server ready" in f.read():
                        booted = True
                        break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never became ready")
            return 1

        # ---- clients A (actor) + B (watcher) ----------------------------
        a = StackClient(args.host, args.port, "StackOne", verbose=args.verbose)
        print("stage: actor A handshake+login")
        if not a.handshake() or not a.login():
            print("FAIL: A handshake/login")
            return 1
        a.drain(4.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(1.5)
        if a.eid == 0:
            print("FAIL: A never received StartGame")
            return 1

        b = StackClient(args.host, args.port, "StackTwo", verbose=args.verbose)
        print("stage: watcher B handshake+login")
        if not b.handshake() or not b.login():
            print("FAIL: B handshake/login")
            return 1
        b.drain(4.0)
        b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        b.drain(1.5)
        if b.eid == 0:
            print("FAIL: B never received StartGame")
            return 1

        sx, sy, sz = a.spawn
        a.move_to(sx + 0.5, sy + 1.62, sz + 0.5)
        b.move_to(sx + 0.5, sy + 1.62, sz + 0.5)
        time.sleep(0.6)
        a.drain(0.6)
        b.drain(0.6)

        def gamemode(g: str):
            a.say(f"/gamemode {g}")
            time.sleep(0.6)
            a.drain(0.6)

        def grant(slot, item_id, count, meta=0, nbt=b""):
            gamemode("1")
            a.creative_take_nbt(slot, item_id, count, meta, nbt)
            time.sleep(0.4)
            a.drain(0.4)
            gamemode("0")

        def bow_slots(client):
            return {idx: st for idx, st in client.inv_slots.items()
                    if st[0] == BOW}

        def illegal_bow_state(client):
            states = list(client.inv_slots.values())
            states += [(w[2], w[3], w[4]) for w in client.slot_writes_nbt
                       if w[0] == 0]
            return [st for st in states if st[0] == BOW and st[2] != 1]

        def bow_write_slots(client):
            return {w[1] for w in client.slot_writes_nbt
                    if w[0] == 0 and w[2] == BOW and w[4] == 1}

        # ================= Phase 1: single creative take =================
        print("stage: phase 1 - one creative bow take = one bow")
        grant(0, BOW, 1)
        ok = drain_until(lambda: a.inv.get((BOW, 0), 0) == 1, [a], 4.0)
        check(ok, "creative take yields exactly one bow (count=1)",
              f"inv={a.inv} slots={a.inv_slots}")

        # ================= Phase 2: two bows die + pickup =================
        print("stage: phase 2 - two bows: death drops split, pickup never fuses")
        grant(1, BOW, 1)
        ok = drain_until(lambda: a.inv.get((BOW, 0), 0) == 2, [a], 4.0)
        check(ok, "second bow sits in its own slot",
              f"inv={a.inv} slots={a.inv_slots}")
        check(len(bow_slots(a)) == 2 and not illegal_bow_state(a),
              "two bows occupy two separate legal slots",
              f"slots={a.inv_slots}")

        a.move_to(sx + 0.5, sy + 0.01, sz + 6.5)
        time.sleep(0.4)
        a.drain(0.4)
        death_x, death_y, death_z = sx + 0.5, sy + 0.01, sz + 6.5

        a.drops_nbt.clear()
        console("/kill StackOne")
        got = drain_until(
            lambda: len([d for d in a.drops_nbt if d[1] == BOW]) >= 2,
            [a], 6.0)
        bow_drops = [d for d in a.drops_nbt if d[1] == BOW]
        check(got, "death drops every bow as its own item entity",
              f"drops={a.drops_nbt}")
        check(all(d[2] == 1 for d in bow_drops),
              "no drop entity fuses two bows into count=2",
              f"bow_drops={bow_drops}")

        time.sleep(2.0)  # death screen; pickup delay drains
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        empty = drain_until(lambda: len(a.inv) == 0, [a], 6.0)
        check(empty, "respawned inventory is empty", f"inv={a.inv}")

        a.inv_slots.clear()
        a.slot_writes_nbt.clear()
        a.move_to(death_x, death_y, death_z)
        picked = drain_until(lambda: a.inv.get((BOW, 0), 0) >= 2, [a, b], 8.0)
        check(picked, "both bows are picked up again",
              f"inv={a.inv} slots={a.inv_slots}")
        check(len(bow_slots(a)) == 2 and not illegal_bow_state(a),
              "picked-up bows sit in two legal count=1 slots (no fusion)",
              f"slots={a.inv_slots} writes={a.slot_writes_nbt}")

        # ================= Phase 3: held bow visible to the watcher ======
        print("stage: phase 3 - picked-up bow is visible to the watcher")
        bow_idx = sorted(bow_slots(a))[0]
        b.me_pkts.clear()
        a.mob_equipment(bow_idx, BOW)
        seen = drain_until(
            lambda: any(m[0] == a.eid and m[1] == BOW for m in b.me_pkts),
            [a, b], 4.0)
        me_bow = [m for m in b.me_pkts if m[0] == a.eid and m[1] == BOW]
        check(seen and all(m[2] == 1 for m in me_bow),
              "watcher receives MobEquipment for the bow with count=1",
              f"me_pkts={b.me_pkts}")

        # ================= Phase 4: picked-up bow still fires ============
        print("stage: phase 4 - a bow from the pickup cycle still fires")
        grant(2, ARROW, 5)
        a.mob_equipment(bow_idx, BOW)
        time.sleep(0.4)
        a.drain(0.4)
        b.entities.clear()
        a.move_to(sx + 0.5, sy + 0.01, sz + 0.5, yaw=0.0, pitch=90.0)
        time.sleep(0.4)
        a.drain(0.4)
        a.use_item_air(BOW)
        time.sleep(1.2)
        a.drain(1.2)
        a.release_item()
        fired = drain_until(lambda: any(t == 80 for _, t in a.entities) or
                            any(t == 80 for _, t in b.entities), [a, b], 8.0)
        check(fired, "released bow (after death+pickup) spawns an arrow",
              f"a_entities={a.entities} b_entities={b.entities}")

        # ================= Phase 5: illegal write is clamped =============
        print("stage: phase 5 - legacy count=2 bow write is clamped")
        a.slot_writes_nbt.clear()
        gamemode("1")
        a.creative_take_nbt(4, BOW, 2)  # illegal: bows never stack
        gamemode("0")
        settled = drain_until(
            lambda: 4 in bow_write_slots(a) and len(bow_write_slots(a)) >= 2,
            [a], 4.0)
        bad_writes = [w for w in a.slot_writes_nbt
                      if w[0] == 0 and w[2] == BOW and w[4] != 1]
        check(settled and not bad_writes,
              "clamped write acks slot=4 count=1 and re-homes the overflow bow",
              f"write_slots={bow_write_slots(a)} bad={bad_writes}")
        check(not illegal_bow_state(a),
              "no slot ends up with an illegal bow stack",
              f"slots={a.inv_slots}")

        # ================= Phase 6: named bow survives death =============
        print("stage: phase 6 - named bow NBT survives the death round trip")
        blob = display_nbt("Arco de Dr1xy")
        grant(5, BOW, 1, nbt=blob)
        named = drain_until(
            lambda: any(w[2] == BOW and w[5] == blob
                        for w in a.slot_writes_nbt), [a], 4.0)
        check(named, "named bow accepted with its NBT blob",
              f"writes={[(w[1], w[2], w[4]) for w in a.slot_writes_nbt][-6:]}")

        a.move_to(sx + 0.5, sy + 0.01, sz + 10.5)
        time.sleep(0.4)
        a.drain(0.4)
        death2_x, death2_y, death2_z = sx + 0.5, sy + 0.01, sz + 10.5
        a.drops_nbt.clear()
        console("/kill StackOne")
        named_drop = drain_until(
            lambda: any(d[1] == BOW and d[4] == blob for d in a.drops_nbt),
            [a], 6.0)
        check(named_drop, "death drop entity carries the bow's NBT",
              f"drops={[(d[1], d[2], d[4][:8]) for d in a.drops_nbt]}")

        time.sleep(2.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        drain_until(lambda: len(a.inv) == 0, [a], 6.0)
        a.slot_writes_nbt.clear()
        a.move_to(death2_x, death2_y, death2_z)
        named_back = drain_until(
            lambda: any(w[2] == BOW and w[4] == 1 and w[5] == blob
                        for w in a.slot_writes_nbt), [a, b], 8.0)
        check(named_back, "re-picked named bow keeps its NBT and count=1",
              f"writes={[(w[1], w[2], w[4], w[5][:8]) for w in a.slot_writes_nbt][-8:]}")

        console("stop")
        time.sleep(1.0)
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    print()
    if failures:
        print(f"FAILED ({len(failures)}): " + "; ".join(failures))
        return 1
    print("ALL CHECKS PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
