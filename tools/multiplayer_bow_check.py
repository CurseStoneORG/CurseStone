#!/usr/bin/env python3
"""End-to-end multiplayer bow + held-item visibility check for CurseStone.

Two real protocol-84 clients (MCPE 0.15.10) connect to a self-managed flat
server: client A (shooter) and client B (watcher). The suite verifies the
multiplayer sync paths that other clients depend on:

  0. AddPlayerPacket carries the shooter's held bow on first view
  1. hotbar switches re-broadcast MobEquipmentPacket to the watcher
  2. a plain block TAP with a bow raises NO action pose for the watcher
     (block-use packets must never start the tension animation, they can
     only cancel it); the real draw-at-block flow (touch-down use packet
     + air charge packets) still raises the pose
  3. releasing spawns the arrow for the watcher: AddEntityPacket(type 80)
     + SetEntityMotionPacket, and NO phantom EntityEvent(HURT) on the
     shooter (the shooter is immune to their own arrow)
  4. the arrow MOVES for the watcher: per-tick MoveEntityPacket stream
     with distinct positions (never frozen at the launch point)
  5. the action pose clears after the release
  6. the arrow AddEntityPacket is wire-conformant: 0x7f metadata
     terminator after the shooter-id LONG entry, then empty links
  7. the air (face 0xFF) draw path still raises/clears the pose, and the
     launch sound (LevelEvent 1002) reaches the watcher

    python3 tools/multiplayer_bow_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import base64
import json
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

# Game-layer ids (protocol 84 / Info.h).
ADD_PLAYER = 0x0A
ADD_ENTITY = 0x0B
REMOVE_ENTITY = 0x0C
MOVE_ENTITY = 0x0F
MOVE_PLAYER = 0x10
LEVEL_EVENT = 0x16
ENTITY_EVENT = 0x18
MOB_EQUIPMENT = 0x1B
USE_ITEM = 0x1F
PLAYER_ACTION = 0x20
SET_ENTITY_DATA = 0x22
SET_ENTITY_MOTION = 0x23
SET_HEALTH = 0x25
CONTAINER_SET_SLOT = 0x2C

# Items / events.
BOW = 261
ARROW = 262
ACTION_RELEASE_ITEM = 5
HURT_ANIMATION = 2
EVENT_SOUND_SHOOT = 1002
FLAG_ACTION_BIT = 0x10  # DATA_FLAG_ACTION inside the PROP_FLAGS byte

# Flat world layout (grass surface at y=3, spawn 128,4,128).
SHOOTER = (128, 4, 128)
DRAW_TARGET_BLOCK = (130, 3, 128)
WATCHER = (128, 4, 122)


def b64u(obj) -> str:
    raw = json.dumps(obj, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


class BowClient(Client):
    """door_bed_flow_check Client + per-name login uuid, bow/equipment
    packet parsing and the game helpers this suite needs."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.equip_pkts = []     # (eid, itemId, meta)
        self.data_flags = []     # (eid, flagsByte) from SetEntityData index-0 BYTE
        self.add_entities = []   # (eid, type, x, y, z, raw payload)
        self.motions = []        # (eid, mx, my, mz)
        self.move_entities = []  # (eid, x, y, z)
        self.entity_events = []  # (eid, event)
        self.level_events = []   # evid
        self.healths = []        # SetHealth values addressed to us
        self.add_players = []    # (eid, heldItemId, heldMeta)

    # ---- login with a per-name uuid (two clients must not collide) ------
    def login(self) -> bool:
        uid = "11111111-2222-3333-4444-%012d" % (abs(hash(self.name)) % 10**12)
        chain_jwt = (b64u({"alg": "ES384"}) + "." +
                     b64u({"displayName": self.name,
                           "identity": uid,
                           "extraData": {}}) + ".sig")
        chain = json.dumps({"chain": [chain_jwt]}, separators=(",", ":")).encode()
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
        if pid == MOB_EQUIPMENT and len(p) >= 11:
            eid = struct.unpack(">q", p[1:9])[0]
            sid = struct.unpack(">h", p[9:11])[0]
            meta = 0
            off = 11
            if sid > 0 and off + 5 <= len(p):
                meta = struct.unpack(">h", p[off + 1:off + 3])[0]
            self.equip_pkts.append((eid, sid, meta))
            return
        if pid == SET_ENTITY_DATA and len(p) >= 10:
            eid = struct.unpack(">q", p[1:9])[0]
            flags = None
            off = 9
            while off < len(p):
                key = p[off]
                off += 1
                if key == 0x7F:
                    break
                typ = (key >> 5) & 0x07
                if typ == 0:    # BYTE
                    if off < len(p):
                        if (key & 0x1F) == 0 and flags is None:
                            flags = p[off]
                        off += 1
                elif typ == 1:  # SHORT
                    off += 2
                elif typ in (2, 3):  # INT / FLOAT
                    off += 4
                elif typ == 7:  # LONG
                    off += 8
                elif typ == 4:  # STRING
                    if off + 2 <= len(p):
                        off += 2 + struct.unpack("<H", p[off:off + 2])[0]
                    else:
                        break
                elif typ == 6:  # POS
                    off += 12
                else:
                    break
            if flags is not None:
                self.data_flags.append((eid, flags))
            return
        if pid == ADD_ENTITY and len(p) >= 52:
            eid = struct.unpack(">q", p[1:9])[0]
            etype = struct.unpack(">i", p[9:13])[0]
            x, y, z = struct.unpack(">fff", p[13:25])
            self.add_entities.append((eid, etype, x, y, z, bytes(p)))
            return
        if pid == SET_ENTITY_MOTION and len(p) >= 21:
            eid = struct.unpack(">q", p[1:9])[0]
            mx, my, mz = struct.unpack(">fff", p[9:21])
            self.motions.append((eid, mx, my, mz))
            return
        if pid == MOVE_ENTITY and len(p) >= 24:
            eid = struct.unpack(">q", p[1:9])[0]
            x, y, z = struct.unpack(">fff", p[9:21])
            self.move_entities.append((eid, x, y, z))
            return
        if pid == ENTITY_EVENT and len(p) >= 10:
            eid = struct.unpack(">q", p[1:9])[0]
            self.entity_events.append((eid, p[9]))
            return
        if pid == LEVEL_EVENT and len(p) >= 5:
            self.level_events.append(struct.unpack(">h", p[1:3])[0])
            return
        if pid == SET_HEALTH and len(p) >= 5:
            self.healths.append(struct.unpack(">i", p[1:5])[0])
            return
        if pid == ADD_PLAYER:
            try:
                off = 17
                nlen = struct.unpack(">H", p[off:off + 2])[0]
                off += 2 + nlen          # username
                if off + 44 > len(p):
                    raise ValueError(f"short AddPlayer payload ({len(p)}B: {p[:40].hex()})")
                eid = struct.unpack(">q", p[off:off + 8])[0]
                off += 8 + 24            # eid + x/y/z + speed x/y/z
                off += 12                # yaw, headYaw, pitch
                held = 0
                hmeta = 0
                if off + 2 <= len(p):
                    held = struct.unpack(">h", p[off:off + 2])[0]
                    if held > 0 and off + 5 <= len(p):
                        hmeta = struct.unpack(">h", p[off + 3:off + 5])[0]
                self.add_players.append((eid, held, hmeta, bytes(p).hex()))
            except (struct.error, ValueError) as exc:
                self.add_players.append((-1, 0, str(exc)))
            return
        Client.on_game_packet(self, p)

    # ---- helpers ---------------------------------------------------------
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

    def draw_air(self, item_id):
        self.use_item(0, 0, 0, 0xFF, item_id)

    def draw_block(self, x, y, z, item_id):
        self.use_item(x, y, z, 1, item_id)

    def release(self):
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


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone multiplayer bow check")
    ap.add_argument("--server", default="/home/z/my-project/CurseStone/build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19141)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_bow_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=BowCheck\nview-distance=4\ngamemode=0\n"
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
            print("FAIL: server never printed Done")
            return 1

        a = BowClient(args.host, args.port, "BowOne", verbose=args.verbose)
        print("stage: shooter A handshake+login")
        if not a.handshake() or not a.login():
            print("FAIL: A handshake/login")
            return 1
        a.drain(4.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(1.5)
        if a.eid == 0:
            print("FAIL: A never received StartGame (no eid)")
            return 1
        print(f"  A eid={a.eid}")

        # Stock the bow + arrows in creative, then return to survival so
        # the arrow-consumption and damage paths are live. Grant order
        # matters: every creative slot write in 0..8 repoints the held
        # hotbar link (setHeldSlot), so the BOW must be granted LAST.
        a.say("/gamemode 1")
        time.sleep(0.8)
        a.drain(0.8)
        a.creative_take(1, ARROW, 5, 0)
        a.creative_take(0, BOW, 1, 0)
        time.sleep(0.6)
        a.drain(0.6)
        a.say("/gamemode 0")
        time.sleep(0.8)
        a.drain(0.8)
        a.mob_equipment(0, BOW, 0)
        time.sleep(0.4)
        a.drain(0.4)

        # Watcher joins AFTER the shooter holds the bow.
        b = BowClient(args.host, args.port, "BowTwo", verbose=args.verbose)
        print("stage: watcher B handshake+login")
        if not b.handshake() or not b.login():
            print("FAIL: B handshake/login")
            return 1
        b.drain(4.0)
        b.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        b.move_to(WATCHER[0] + 0.5, WATCHER[1], WATCHER[2] + 0.5)
        b.drain(1.5)
        if b.eid == 0:
            print("FAIL: B never received StartGame (no eid)")
            return 1
        print(f"  B eid={b.eid}")

        # -------- 0: AddPlayerPacket carried the held bow ----------------
        saw_a = [e for e in b.add_players if e[0] == a.eid]
        check(bool(saw_a) and saw_a[0][1] == BOW,
              "AddPlayerPacket shows the held bow to a new viewer",
              f"add_players={b.add_players}")

        # -------- 1: hotbar switches re-broadcast MobEquipment -----------
        mark = len(b.equip_pkts)
        a.mob_equipment(1, ARROW, 0)
        time.sleep(0.8)
        b.drain(1.0)
        got_arrow = [e for e in b.equip_pkts[mark:]
                     if e[0] == a.eid and e[1] == ARROW]
        check(bool(got_arrow),
              "watcher sees MobEquipment(arrow) after a slot switch",
              f"equip={b.equip_pkts[mark:]}")
        mark = len(b.equip_pkts)
        a.mob_equipment(0, BOW, 0)
        time.sleep(0.8)
        b.drain(1.0)
        got_bow = [e for e in b.equip_pkts[mark:]
                   if e[0] == a.eid and e[1] == BOW]
        check(bool(got_bow),
              "watcher sees MobEquipment(bow) after switching back",
              f"equip={b.equip_pkts[mark:]}")

        # -------- 2: block tap raises NO pose; draw-at-block still works --
        mark = len(b.data_flags)
        a.draw_block(*DRAW_TARGET_BLOCK, BOW)
        time.sleep(0.8)
        b.drain(1.0)
        pose_on = [f for (eid, f) in b.data_flags[mark:]
                   if eid == a.eid and (f & FLAG_ACTION_BIT)]
        check(not pose_on,
              "plain block tap with a bow raises NO action pose for the watcher",
              f"flags={b.data_flags[mark:]}")

        # The real draw-at-block flow: touch-down use packet on the block,
        # then the air-use charge packets, must raise the pose.
        a.draw_block(*DRAW_TARGET_BLOCK, BOW)
        a.draw_air(BOW)
        time.sleep(0.8)
        b.drain(1.0)
        pose_on = [f for (eid, f) in b.data_flags
                   if eid == a.eid and (f & FLAG_ACTION_BIT)]
        check(bool(pose_on),
              "block-targeted bow draw (touch + air charge) raises DATA_FLAG_ACTION",
              f"flags={b.data_flags}")

        # -------- 3: release spawns the arrow, no phantom hurt -----------
        b.add_entities.clear()
        b.motions.clear()
        b.entity_events.clear()
        a.healths.clear()
        time.sleep(1.05)  # ~21 ticks of draw at 20 tps -> full power
        b.move_marks = len(b.move_entities)
        a.release()
        b.drain(3.5)
        a.drain(0.5)

        arrows = [e for e in b.add_entities if e[1] == 80 and e[0] != a.eid]
        check(bool(arrows),
              "watcher receives AddEntityPacket(type 80 ARROW)",
              f"add_entities={[(e[0], e[1]) for e in b.add_entities]}")
        arrow_eid = arrows[0][0] if arrows else None
        hurt_a = [e for e in b.entity_events
                  if e[0] == a.eid and e[1] == HURT_ANIMATION]
        check(not hurt_a,
              "no phantom EntityEvent(HURT) on the shooter for the watcher",
              f"events={b.entity_events}")
        check(not a.healths,
              "shooter's own health untouched by their own shot",
              f"healths={a.healths}")
        if arrow_eid is not None:
            check(any(m[0] == arrow_eid for m in b.motions),
                  "watcher receives SetEntityMotionPacket for the arrow",
                  f"motions={b.motions}")
        else:
            check(False, "watcher receives SetEntityMotionPacket for the arrow",
                  "no arrow entity")

        # -------- 4: the arrow MOVES for the watcher ---------------------
        if arrow_eid is not None:
            path = [(m[1], m[2], m[3]) for m in b.move_entities
                    if m[0] == arrow_eid]
            distinct = len(set(path))
            check(len(path) >= 6 and distinct >= 3,
                  "arrow streams MoveEntityPacket positions (never frozen)",
                  f"n={len(path)} distinct={distinct}")
        else:
            check(False, "arrow streams MoveEntityPacket positions (never frozen)",
                  "no arrow entity")

        # -------- 5: the pose clears after release -----------------------
        pose_off = [f for (eid, f) in b.data_flags
                    if eid == a.eid and not (f & FLAG_ACTION_BIT)]
        check(bool(pose_off),
              "action pose clears for the watcher after the release",
              f"flags={b.data_flags}")

        # -------- 6: AddEntityPacket wire conformance --------------------
        if arrows:
            raw = arrows[0][5]
            tail_ok = raw[-3:] == b"\x7f\x00\x00"
            # Shooter-id LONG entry: key = (TYPE_LONG << 5) | 17 = 0xF1,
            # then the 8 little-endian metadata long (the shooter's eid).
            shooter_entry = (b"\xf1" + struct.pack("<q", a.eid)) in raw
            check(tail_ok and shooter_entry,
                  "arrow AddEntityPacket ends 0x7f + empty links and "
                  "carries the shooter-id metadata",
                  f"tail={raw[-4:].hex()} shooter_entry={shooter_entry}")
        else:
            check(False, "arrow AddEntityPacket wire conformance", "no arrow entity")

        # -------- 7: air draw path + launch sound ------------------------
        b.add_entities.clear()
        b.entity_events.clear()
        a.healths.clear()
        mark = len(b.data_flags)
        a.draw_air(BOW)
        time.sleep(0.8)
        b.drain(1.0)
        pose_on_air = [f for (eid, f) in b.data_flags[mark:]
                       if eid == a.eid and (f & FLAG_ACTION_BIT)]
        check(bool(pose_on_air),
              "air (face 0xFF) bow draw still raises the pose",
              f"flags={b.data_flags[mark:]}")
        time.sleep(1.05)
        a.release()
        b.drain(3.0)
        a.drain(0.5)
        hurt_a2 = [e for e in b.entity_events
                   if e[0] == a.eid and e[1] == HURT_ANIMATION]
        check(not hurt_a2,
              "second shot also produces no phantom hurt event",
              f"events={b.entity_events}")
        check(EVENT_SOUND_SHOOT in b.level_events,
              "watcher hears the bow launch sound (LevelEvent 1002)",
              f"level_events={sorted(set(b.level_events))[:12]}")

    finally:
        try:
            console("stop")
        except Exception:
            pass
        time.sleep(0.5)
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s): " + "; ".join(failures))
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
