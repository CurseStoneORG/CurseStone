#!/usr/bin/env python3
"""Hotbar-link regression check for CurseStone (creative take desync).

Reproduces the reported bug wire-exactly: a creative client takes an item
from the creative menu while a NON-FIRST hotbar position is selected. The
0.15.10 client writes the stack to inventory slot 0 (ContainerSetSlot) and
then re-sends MobEquipment with the SAME selected position, whose padded
absolute-slot byte (position + 9) now links that position to the written
slot. The reference server remaps the hotbar link exactly there
(setHeldItemIndex(selectedSlot, false, slot)); a server that ignores the
link keeps the identity mapping, so the holder reads an empty stack (bow
unshootable) and viewers see no hand item — and, inverted, viewers saw the
bow while the client showed an empty first slot.

Checks (client A = creative shooter, client B = watcher):

  1. creative take + equip re-affirmation (abs=9, pos=1) makes B receive
     MobEquipment(itemId=BOW, slotByte=0, posByte=1) — B sees the bow
  2. releasing that bow spawns the arrow for B (AddEntity type 80 + motion)
     — the previously missing projectile
  3. switching to position 0 (linked to the empty slot 1, abs=10) makes B
     receive MobEquipment(itemId=0) — B sees the empty hand the client shows
  4. a link-ONLY change on the held position (abs=11, pos=0, same position
     byte) must still rebroadcast — pre-fix this packet was swallowed
  5. a normal position switch afterwards still works (identity sanity)

    python3 tools/hotbar_link_check.py [--server /path/to/CurseStone]

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
MOVE_ENTITY = 0x0F
MOVE_PLAYER = 0x10
MOB_EQUIPMENT = 0x1B
USE_ITEM = 0x1F
PLAYER_ACTION = 0x20
CONTAINER_SET_SLOT = 0x2C
SET_ENTITY_MOTION = 0x23

BOW = 261
ARROW_ENTITY_TYPE = 80
ACTION_RELEASE_ITEM = 5

# Flat world layout (grass surface at y=3, spawn 128,4,128).
SHOOTER = (128, 4, 128)
DRAW_TARGET_BLOCK = (130, 3, 128)
WATCHER = (128, 4, 122)


class LinkClient(Client):
    """Client + MobEquipment/arrow capture and the senders this suite needs."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.equip_pkts = []    # (eid, itemId, slotByte, posByte)
        self.add_entities = []  # (eid, type, x, y, z)
        self.motions = []       # (eid, mx, my, mz)

    # ---- login with a per-name uuid (two clients must not collide) ------
    def login(self) -> bool:
        import base64
        import json
        import zlib

        def b64u(obj) -> str:
            raw = json.dumps(obj, separators=(",", ":")).encode()
            return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")

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
            off = 11
            b1 = b2 = -1
            if sid > 0 and off + 5 <= len(p):
                # count(1) + damage(2) + nbtLen(2 LE) [+ nbt]
                nbt_len = struct.unpack("<H", p[off + 3:off + 5])[0]
                off += 5 + nbt_len
            if off + 2 <= len(p):
                b1, b2 = p[off], p[off + 1]
            self.equip_pkts.append((eid, sid, b1, b2))
            return
        if pid == ADD_ENTITY and len(p) >= 13:
            eid = struct.unpack(">q", p[1:9])[0]
            etype = struct.unpack(">i", p[9:13])[0]
            self.add_entities.append((eid, etype))
            return
        if pid == SET_ENTITY_MOTION and len(p) >= 9:
            eid = struct.unpack(">q", p[1:9])[0]
            self.motions.append((eid,))
            return
        Client.on_game_packet(self, p)

    # ---- senders ---------------------------------------------------------
    def creative_take(self, slot, item_id, count=1, meta=0):
        # ContainerSetSlot window 0: the client's creative-take write.
        cs = bytes([CONTAINER_SET_SLOT, 0])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", 0)
        cs += struct.pack(">h", item_id)
        cs += bytes([count])
        cs += struct.pack(">h", meta)
        cs += struct.pack("<H", 0)
        self.send_game(cs)

    def mob_equipment_raw(self, abs_slot, position, item_id, count=1, meta=0):
        # MobEquipment with BOTH wire bytes under test control:
        # byte 1 = padded absolute inventory slot (position + 9, 255 = blank),
        # byte 2 = selected hotbar position 0..8.
        # An empty item (id 0) encodes as ONLY the 2-byte zero id — the
        # server's readSlot returns right after the id, exactly like the
        # reference getSlot().
        me = bytes([MOB_EQUIPMENT])
        me += struct.pack(">q", self.eid)
        me += struct.pack(">h", item_id)
        if item_id > 0:
            me += bytes([count])
            me += struct.pack(">h", meta)
            me += struct.pack("<H", 0)
        me += bytes([abs_slot, position])
        self.send_game(me)

    def draw_block(self, x, y, z, item_id):
        self.use_item(x, y, z, 1, item_id)

    def draw_air(self, item_id):
        self.use_item(0, 0, 0, 0xFF, item_id)

    def release(self):
        self.player_action(ACTION_RELEASE_ITEM)

    def move_to(self, x, y, z, yaw=0.0, pitch=0.0):
        body = (bytes([MOVE_PLAYER]) + struct.pack(">q", self.eid) +
                struct.pack(">fff", x, y, z) +
                struct.pack(">fff", yaw, yaw, pitch) +
                bytes([0, 1]))
        self.send_game(body)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone hotbar-link check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19147)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_hblink_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=HotbarLinkCheck\nview-distance=4\ngamemode=1\n"
                "advanced-log=true\n" % args.port)
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

        a = LinkClient(args.host, args.port, "LinkOne", verbose=args.verbose)
        print("stage: creative shooter A handshake+login")
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

        b = LinkClient(args.host, args.port, "LinkTwo", verbose=args.verbose)
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

        # -------- 1: creative take + equip re-affirmation (the repro) -----
        # The 0.15.10 creative take: write the stack to inventory slot 0,
        # then MobEquipment(abs=0+9, pos=1) linking selected position 1 to it.
        mark = len(b.equip_pkts)
        a.creative_take(0, BOW, 1, 0)
        time.sleep(0.5)
        a.drain(0.4)
        a.mob_equipment_raw(9, 1, BOW, 1, 0)
        time.sleep(0.8)
        b.drain(1.0)
        got = [e for e in b.equip_pkts[mark:]
               if e[0] == a.eid and e[1] == BOW and e[2] == 0 and e[3] == 1]
        check(bool(got),
              "watcher sees the bow after a creative take into a non-first hotbar position",
              f"equip={b.equip_pkts[mark:]}")

        # -------- 2: that bow actually fires (the missing projectile) ------
        b.add_entities.clear()
        b.motions.clear()
        a.draw_block(*DRAW_TARGET_BLOCK, BOW)
        a.draw_air(BOW)
        time.sleep(1.05)  # ~21 ticks of draw at 20 tps -> full power
        a.release()
        b.drain(3.5)
        a.drain(0.5)
        arrows = [e for e in b.add_entities
                  if e[1] == ARROW_ENTITY_TYPE and e[0] != a.eid]
        check(bool(arrows),
              "releasing the relocated bow spawns the arrow for the watcher",
              f"add_entities={b.add_entities}")
        if arrows:
            arrow_eid = arrows[0][0]
            check(any(m[0] == arrow_eid for m in b.motions),
                  "watcher receives SetEntityMotionPacket for the arrow",
                  f"motions={b.motions}")

        # -------- 3: selecting the client-empty first slot shows empty -----
        mark = len(b.equip_pkts)
        a.mob_equipment_raw(10, 0, 0, 0, 0)
        time.sleep(0.8)
        b.drain(1.0)
        got = [e for e in b.equip_pkts[mark:]
               if e[0] == a.eid and e[1] == 0 and e[2] == 1 and e[3] == 0]
        check(bool(got),
              "watcher sees an empty hand while the client shows the empty first slot",
              f"equip={b.equip_pkts[mark:]}")

        # -------- 4: link-ONLY change on the held position rebroadcasts ----
        mark = len(b.equip_pkts)
        a.mob_equipment_raw(11, 0, 0, 0, 0)
        time.sleep(0.8)
        b.drain(1.0)
        got = [e for e in b.equip_pkts[mark:]
               if e[0] == a.eid and e[1] == 0 and e[2] == 2 and e[3] == 0]
        check(bool(got),
              "same-position relink still rebroadcasts MobEquipment",
              f"equip={b.equip_pkts[mark:]}")

        # -------- 5: normal position switch still works (identity sanity) --
        mark = len(b.equip_pkts)
        a.mob_equipment_raw(10, 2, 0, 0, 0)
        time.sleep(0.8)
        b.drain(1.0)
        got = [e for e in b.equip_pkts[mark:]
               if e[0] == a.eid and e[1] == 0 and e[2] == 1 and e[3] == 2]
        check(bool(got),
              "plain hotbar switch still equips correctly afterwards",
              f"equip={b.equip_pkts[mark:]}")

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
