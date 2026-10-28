#!/usr/bin/env python3
"""End-to-end multiplayer bow/eat/death/arrow check.

Two real protocol-84 clients (MCPE 0.15.10) against a self-managed flat
server:

  Phase 1  eating pose: A holds bread and sends the air-use packet; B must
           receive the using-item pose (DATA_FLAG_ACTION set) while A eats
           and see it cleared after the completion EntityEvent(USE_ITEM).
  Phase 2  bow pose ammo gate: with NO arrows in the inventory the draw
           pose must NOT reach B; after A stocks one arrow the pose must
           reach B, and the released arrow must land, stick, and be
           collectable (self TakeItemEntity eid=0 + arrow back in the
           inventory).
  Phase 3  death drops: /kill drops every non-empty stack as item
           entities, the dead body must NOT re-absorb its own drops, the
           respawned inventory is empty, and another player can walk over
           the drops and collect them.

    python3 tools/mp_bow_death_arrow_check.py [--server BIN] [--port N]

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
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

# Game-layer ids (protocol 84 / Info.h).
ADD_ENTITY = 0x0B
REMOVE_ENTITY = 0x0C
ADD_ITEM_ENTITY = 0x0D
TAKE_ITEM_ENTITY = 0x0E
MOVE_PLAYER = 0x10
PLAYER_ACTION = 0x20
ENTITY_EVENT = 0x18
MOB_EQUIPMENT = 0x1B
SET_ENTITY_DATA = 0x22
CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_CONTENT = 0x2E

ACTION_RELEASE_ITEM = 5
EAT_EVENT = 9  # EntityEvent USE_ITEM ("finished eating/drinking")

BREAD = 297
BOW = 261
ARROW = 262

# Metadata key/type layout (SetEntityDataPacket).
META_TYPE_BYTE = 0
META_INDEX_FLAGS = 0
META_FLAG_ACTION_BIT = 1 << 4

META_SIZE = {
    0: 1,  # byte
    1: 2,  # short (LE on the wire)
    2: 4,  # int
    3: 4,  # float
    4: -1,  # string (u16le length + bytes)
    5: 0,   # nbt (unused)
    6: 12,  # pos
    7: 8,   # long
}


def b64u(obj) -> str:
    import base64
    import json
    raw = json.dumps(obj, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


class MpClient(Client):
    """door_bed_flow_check Client + per-name uuid, pose/flag tracking,
    item-entity and arrow-entity capture, inventory content dumps."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.slot_writes = []    # (win, slot, id, meta, count) slot confirms
        self.inv = {}            # {(id, meta): count} from window-0 dumps
        self.take_targets = []   # TakeItemEntityPacket (target, taker)
        self.removed = []        # RemoveEntityPacket eids
        self.flag_sets = []      # (eid, flags_byte) SetEntityData flags entries
        self.item_drops = []     # AddItemEntityPacket (eid, id, count, meta)
        self.entities = []       # AddEntityPacket (eid, type)
        self.seen_moves = {}     # eid -> (x, y, z) last relayed MovePlayer

    def login(self) -> bool:
        uid = "11111111-2222-3333-4444-%012d" % (abs(hash(self.name)) % 10**12)
        chain_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"displayName": self.name,
                           "identity": uid,
                           "extraData": {}}) + ".sig")
        chain = json_chain(chain_jwt)
        cdata_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"DisplayName": self.name,
                           "Identity": uid,
                           "DeviceModel": "Linux", "DeviceOS": 7,
                           "LanguageCode": "en_US",
                           "SkinId": "Standard_Custom"}) + ".sig")
        raw = struct.pack("<I", len(chain)) + chain + \
            struct.pack("<I", len(cdata_jwt)) + cdata_jwt.encode()
        import zlib
        comp = zlib.compress(raw)
        body = struct.pack(">I", 84) + struct.pack(">I", len(comp)) + comp
        self.send_raw(self.send_encapsulated(bytes([0x01]) + body, reliability=3))
        deadline = time.time() + 5.0
        while time.time() < deadline:
            self.drain(0.2)
            if any(ev[0] in ("update_block", "set_time", "text") for ev in self.events) \
               or self.eid != 0:
                return True
        return self.eid != 0

    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if not hasattr(self, "all_pids"):
            self.all_pids = set()
        self.all_pids.add(pid)
        if pid == TAKE_ITEM_ENTITY and len(p) >= 17:
            tgt, taker = struct.unpack(">qq", p[1:17])
            self.take_targets.append((tgt, taker))
            return
        if pid == REMOVE_ENTITY and len(p) >= 9:
            self.removed.append(struct.unpack(">q", p[1:9])[0])
            return
        if pid == ADD_ENTITY and len(p) >= 13:
            eid, etype = struct.unpack(">qi", p[1:13])
            self.entities.append((eid, etype))
            return
        if pid == ADD_ITEM_ENTITY and len(p) >= 11:
            eid, iid = struct.unpack(">qh", p[1:11])
            if iid > 0 and len(p) >= 14:
                cnt = p[11]
                meta = struct.unpack(">h", p[12:14])[0]
                self.item_drops.append((eid, iid, cnt, meta))
            return
        if pid == MOVE_PLAYER and len(p) >= 30:
            eid = struct.unpack(">q", p[1:9])[0]
            x, y, z = struct.unpack(">fff", p[9:21])
            self.seen_moves[eid] = (round(x, 2), round(y, 2), round(z, 2))
            return
        if pid == SET_ENTITY_DATA and len(p) >= 9:
            eid = struct.unpack(">q", p[1:9])[0]
            flags = self.parse_flags(p[9:])
            if flags is not None:
                self.flag_sets.append((eid, flags))
            return
        if pid == CONTAINER_SET_SLOT and len(p) >= 13:
            win = p[1]
            slot = struct.unpack(">h", p[2:4])[0]
            sid = struct.unpack(">h", p[6:8])[0]
            cnt = p[8]
            meta = struct.unpack(">h", p[9:11])[0]
            self.slot_writes.append((win, slot, sid, meta, cnt))
            return
        if pid == CONTAINER_SET_CONTENT and len(p) >= 4:
            self.parse_inventory(p)
            return
        Client.on_game_packet(self, p)

    @staticmethod
    def parse_flags(buf: bytes):
        """Walk the metadata entries, return the flags byte (index 0,
        byte type) or None when the packet carries no flags entry."""
        off = 0
        while off < len(buf):
            key = buf[off]
            off += 1
            if key == 0x7F:
                return None
            mtype = (key >> 5) & 0x07
            index = key & 0x1F
            size = META_SIZE.get(mtype)
            if size is None:
                return None
            if size == -1:
                if off + 2 > len(buf):
                    return None
                slen = struct.unpack("<H", buf[off:off + 2])[0]
                val = buf[off + 2:off + 2 + slen]
                off += 2 + slen
                continue
            if off + size > len(buf):
                return None
            if mtype == META_TYPE_BYTE and index == META_INDEX_FLAGS:
                return buf[off]
            off += size
        return None

    def parse_inventory(self, p: bytes):
        """window-0 ContainerSetContent dump, meta-aware slot parse."""
        win = p[1]
        count = struct.unpack(">H", p[2:4])[0]
        off = 4
        totals = {}
        for _ in range(count):
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
            totals[(sid, meta)] = totals.get((sid, meta), 0) + cnt
        if win == 0 and off + 2 <= len(p):
            hb = struct.unpack(">H", p[off:off + 2])[0]
            off += 2 + 4 * hb
        if win == 0:
            self.inv = totals

    # ---- helpers ----------------------------------------------------------
    def creative_take(self, slot, item_id, count=1, meta=0):
        cs = bytes([CONTAINER_SET_SLOT, 0])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", 0)
        cs += struct.pack(">h", item_id)
        cs += bytes([count])
        cs += struct.pack(">h", meta)
        cs += struct.pack("<H", 0)
        self.send_game(cs)

    def mob_equipment(self, hotbar, item_id, meta=0):
        me = bytes([MOB_EQUIPMENT])
        me += struct.pack(">q", self.eid)
        me += struct.pack(">h", item_id)
        me += bytes([1])
        me += struct.pack(">h", meta)
        me += struct.pack("<H", 0)
        me += bytes([hotbar + 9, hotbar])
        self.send_game(me)

    def entity_event(self, event):
        self.send_game(bytes([ENTITY_EVENT]) + struct.pack(">q", self.eid) +
                       bytes([event]))

    def use_item_air(self, item_id, meta=0):
        body = (bytes([0x1F]) +
                struct.pack(">iii", 0, 0, 0) + bytes([0xFF]) +
                struct.pack(">fff", 0.0, 0.0, 0.0) +
                struct.pack(">fff", 0.0, 0.0, 0.0) +
                struct.pack(">i", 0) +
                (struct.pack(">h", item_id) + bytes([1]) +
                 struct.pack(">h", meta) + struct.pack("<H", 0)))
        self.send_game(body)

    def release_item(self):
        self.player_action(ACTION_RELEASE_ITEM)

    def move_to(self, x, y, z, yaw=0.0, pitch=0.0):
        body = (bytes([MOVE_PLAYER]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y, z) +
                struct.pack(">fff", yaw, yaw, pitch) +
                bytes([0, 1]))
        self.send_game(body)

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([0x07, 1]) + struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() + struct.pack(">H", len(raw)) + raw)


def json_chain(chain_jwt: str) -> bytes:
    import json
    return json.dumps({"chain": [chain_jwt]}, separators=(",", ":")).encode()


def drain_until(pred, clients, seconds):
    """Drain all clients until pred() is true or the deadline passes."""
    deadline = time.time() + seconds
    while time.time() < deadline:
        if pred():
            return True
        for c in clients:
            c.drain(0.1)
    return pred()


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone bow/eat/death/arrow check")
    ap.add_argument("--server", default="/home/z/my-project/CurseStone/build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19144)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_bowdeath_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=BowDeath\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("BowOne\nBowTwo\n")
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
        a = MpClient(args.host, args.port, "BowOne", verbose=args.verbose)
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
        print(f"  A eid={a.eid} spawn={a.spawn}")

        b = MpClient(args.host, args.port, "BowTwo", verbose=args.verbose)
        print("stage: watcher B handshake+login")
        if not b.handshake() or not b.login():
            print("FAIL: B handshake/login")
            return 1
        # Keep A's session alive while B loads in: the 5s session timeout
        # reaps a client that stops answering keepalive pings, and A only
        # answers while its own drain loop runs. Interleave both drains.
        for _ in range(20):
            b.drain(0.2)
            a.drain(0.2)
        b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        for _ in range(8):
            b.drain(0.2)
            a.drain(0.2)
        if b.eid == 0:
            print("FAIL: B never received StartGame")
            return 1
        print(f"  B eid={b.eid} spawn={b.spawn}")

        sx, sy, sz = a.spawn
        a.move_to(sx + 0.5, sy + 1.62, sz + 0.5)
        b.move_to(sx + 0.5, sy + 1.62, sz + 0.5)
        time.sleep(0.6)
        a.drain(0.6)
        b.drain(0.6)

        def grant(slot, item_id, count, meta=0):
            a.say("/gamemode 1")
            time.sleep(0.6)
            a.drain(0.6)
            a.creative_take(slot, item_id, count, meta)
            time.sleep(0.4)
            a.drain(0.4)
            a.say("/gamemode 0")
            time.sleep(0.6)
            a.drain(0.6)

        def action_raises(client, eid):
            return [f for f in client.flag_sets if f[0] == eid]

        # ================= Phase 1: eating pose ==========================
        print("stage: phase 1 - eating pose visible to the watcher")
        grant(1, BREAD, 3)
        a.mob_equipment(1, BREAD)
        time.sleep(0.4)
        a.drain(0.4)

        b.flag_sets.clear()
        a.use_item_air(BREAD)
        raised = drain_until(
            lambda: any(f[1] & META_FLAG_ACTION_BIT for f in action_raises(b, a.eid)),
            [a, b], 3.0)
        check(raised, "watcher sees the using-item pose while A eats",
              f"flag_sets={action_raises(b, a.eid)}")

        # Re-sent use packets (the real client repeats them) must keep the
        # pose up and must not clear it.
        a.use_item_air(BREAD)
        time.sleep(0.5)
        a.drain(0.5)
        still_up = any(f[1] & META_FLAG_ACTION_BIT for f in action_raises(b, a.eid)[-2:])
        check(still_up, "repeated use packets keep the eat pose up",
              f"flag_sets={action_raises(b, a.eid)}")

        a.entity_event(EAT_EVENT)
        cleared = drain_until(
            lambda: action_raises(b, a.eid) and
            not (action_raises(b, a.eid)[-1][1] & META_FLAG_ACTION_BIT),
            [a, b], 3.0)
        check(cleared, "completion event clears the eat pose for the watcher",
              f"flag_sets={action_raises(b, a.eid)}")

        # ================= Phase 2: bow ammo gate ========================
        print("stage: phase 2 - bow pose needs ammo; arrow pickup")
        grant(2, BOW, 1)
        a.mob_equipment(2, BOW)
        time.sleep(0.4)
        a.drain(0.4)

        b.flag_sets.clear()
        a.use_item_air(BOW)
        time.sleep(1.2)
        a.drain(1.2)
        check(not action_raises(b, a.eid),
              "bow without arrows raises NO draw pose for the watcher",
              f"flag_sets={action_raises(b, a.eid)}")

        grant(3, ARROW, 5)
        a.mob_equipment(2, BOW)
        time.sleep(0.4)
        a.drain(0.4)

        b.flag_sets.clear()
        a.use_item_air(BOW)
        raised = drain_until(
            lambda: any(f[1] & META_FLAG_ACTION_BIT for f in action_raises(b, a.eid)),
            [a, b], 3.0)
        check(raised, "bow with arrows raises the draw pose for the watcher",
              f"flag_sets={action_raises(b, a.eid)}")

        # Aim straight down and release; the arrow must land at A's feet,
        # stick, and be collectable (self take eid=0 + arrow in inventory).
        # B steps out of the line of fire first and A drops to ground level
        # so the pickup volume covers the stuck arrow.
        b.move_to(sx + 0.5, sy + 1.62, sz - 4.5)
        time.sleep(0.3)
        b.drain(0.3)
        a.move_to(sx + 0.5, sy + 0.01, sz + 0.5, yaw=0.0, pitch=90.0)
        time.sleep(0.4)
        a.drain(0.4)
        a.release_item()
        got_back = drain_until(
            lambda: any(t == 0 for _, t in a.take_targets) and
            a.inv.get((ARROW, 0), 0) >= 5 and
            any(t == 80 for _, t in a.entities),
            [a, b], 8.0)
        arrows_shot = [e for e, t in a.entities if t == 80]
        check(bool(arrows_shot), "released bow spawns an arrow entity",
              f"entities={a.entities} all_pids={sorted(getattr(a, 'all_pids', set()))}")
        check(got_back, "stuck arrow is collected (self take eid=0, arrow restocked)",
              f"takes={a.take_targets} arrows={a.inv.get((ARROW, 0), 0)} "
              f"shot={arrows_shot} removed={a.removed}")
        check(any(t == 0 for _, t in a.take_targets),
              "collector receives the eid=0 self take packet",
              f"takes={a.take_targets}")

        # ================= Phase 3: death drops ==========================
        print("stage: phase 3 - death drops with keep-inventory: false")
        grant(4, BREAD, 2)  # extra stack so /kill drops several entities
        # Die away from the respawn point so the respawned player cannot
        # race the watcher to the drops.
        a.move_to(sx + 0.5, sy + 0.01, sz + 6.5)
        time.sleep(0.4)
        a.drain(0.4)
        death_x, death_y, death_z = sx + 0.5, sy + 0.01, sz + 6.5

        b.item_drops.clear()
        b.take_targets.clear()
        console("/kill BowOne")
        got_drops = drain_until(
            lambda: {iid for _, iid, _, _ in b.item_drops} >= {BREAD, BOW, ARROW},
            [a, b], 6.0)
        check(got_drops, "every stack is broadcast as an item entity on death",
              f"drops={b.item_drops}")

        # The dead body must NOT re-absorb its own drops: no take addressed
        # to A may arrive while the death screen is open.
        time.sleep(2.0)
        a.drain(2.0)
        b.drain(0.5)
        stolen = [t for t in b.take_targets if t[1] == a.eid]
        check(not stolen, "dead player does not re-absorb its own drops",
              f"takes={b.take_targets}")

        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        empty = drain_until(
            lambda: a.inv is not None and len(a.inv) == 0,
            [a], 6.0)
        check(empty, "respawned inventory is empty (drop run confirmed)",
              f"inv={a.inv}")

        b.move_to(death_x, death_y, death_z)
        collected = drain_until(
            lambda: b.inv.get((BREAD, 0), 0) >= 5 and
            b.inv.get((ARROW, 0), 0) >= 4,
            [a, b], 8.0)
        check(collected, "another player collects the death drops",
              f"inv={b.inv}")

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
