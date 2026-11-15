#!/usr/bin/env python3
"""End-to-end structure-chest generation check for CurseStone.

The two bugs this locks down:

  A. structures had no chest loot at all — StructurePiece::createChest was
     commented out (no block, no tile, no loot) and the dungeon
     (MonsterRoomFeature) placed the chest BLOCK without ever creating its
     tile, so dungeons rendered an invisible chest with nothing inside.
     In MCPE 0.15 the chest model is drawn by the client's block actor,
     which only exists once the BlockEntityDataPacket with the tile
     compound arrives — a chest block without its tile is air to the
     client.

  B. the tile that does exist must carry loot: the generation-time blob
     (id/x/y/z + Items) has to reach tile_entities, hydrate into a
     TileChest with contents and stream out with the chunk.

One real protocol-84 client (MCPE 0.15.10) boots into a normal-generator
world (fixed seed), teleports across a grid of points so the server
streams fresh chunks at every stop, and every BlockEntityDataPacket the
chunk stream carries is parsed. The check then asserts:

  1. chest tiles arrive AT ALL (a tile = an actor = a visible chest)
  2. generated chests arrive WITH loot (Items non-empty, entries valid:
     Slot 0..26, id > 0, Count > 0)
  3. the loot spans several item types (a pool, not a single hard-coded
     stack) across the whole sweep
  4. the server survives the whole sweep (no crash during population)

    python3 tools/structure_chest_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import json
import os
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from door_bed_flow_check import Client, PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

BLOCK_ENTITY_DATA = 0x32

TAG_END = 0
TAG_BYTE = 1
TAG_SHORT = 2
TAG_INT = 3
TAG_LONG = 4
TAG_FLOAT = 5
TAG_DOUBLE = 6
TAG_BYTE_ARRAY = 7
TAG_STRING = 8
TAG_LIST = 9
TAG_COMPOUND = 10
TAG_INT_ARRAY = 11


class NbtReader:
    """Minimal little-endian NBT reader — exactly what the server writes
    for tiles (nbt::write, little_endian=true: u16 LE string lengths, LE
    numerics, classic tag-type + name headers)."""

    def __init__(self, data: bytes):
        self.d = data
        self.o = 0

    def _u8(self):
        v = self.d[self.o]
        self.o += 1
        return v

    def _le(self, n):
        v = int.from_bytes(self.d[self.o:self.o + n], "little", signed=True)
        self.o += n
        return v

    def _u16(self):
        v = int.from_bytes(self.d[self.o:self.o + 2], "little")
        self.o += 2
        return v

    def _string(self):
        n = self._u16()
        s = self.d[self.o:self.o + n]
        self.o += n
        return s.decode("utf-8", "replace")

    def value(self, tag_type):
        if tag_type == TAG_BYTE:
            return self._le(1)
        if tag_type == TAG_SHORT:
            return self._le(2)
        if tag_type == TAG_INT:
            return self._le(4)
        if tag_type == TAG_LONG:
            return self._le(8)
        if tag_type == TAG_FLOAT:
            return struct.unpack("<f", self.d[self.o:self.o + 4])[0]
        if tag_type == TAG_DOUBLE:
            return struct.unpack("<d", self.d[self.o:self.o + 8])[0]
        if tag_type == TAG_BYTE_ARRAY:
            n = self._le(4)
            b = self.d[self.o:self.o + n]
            self.o += n
            return b
        if tag_type == TAG_STRING:
            return self._string()
        if tag_type == TAG_LIST:
            item_type = self._u8()
            n = self._le(4)
            return [self.value(item_type) for _ in range(max(0, n))]
        if tag_type == TAG_COMPOUND:
            out = {}
            while True:
                t = self._u8()
                if t == TAG_END:
                    return out
                name = self._string()
                out[name] = self.value(t)
        if tag_type == TAG_INT_ARRAY:
            n = self._le(4)
            vals = [self._le(4) for _ in range(max(0, n))]
            return vals
        raise ValueError(f"unknown tag type {tag_type}")

    def root(self):
        t = self._u8()
        name = self._string()
        return name, self.value(t)


class ChestClient(Client):
    """Client + BlockEntityData capture (the chunk-stream tile flow)."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.tiles = []  # (x, y, z, compound)

    def on_game_packet(self, p: bytes):
        if not p:
            return
        if p[0] == BLOCK_ENTITY_DATA and len(p) >= 13:
            x, y, z = struct.unpack(">iii", p[1:13])
            try:
                _, tag = NbtReader(p[13:]).root()
            except (ValueError, IndexError, struct.error):
                tag = None
            if tag is not None:
                self.tiles.append((x, y, z, tag))
            return
        Client.on_game_packet(self, p)

    def say(self, msg):
        raw = msg.encode()
        self.send_game(bytes([0x07, 1]) +
                       struct.pack(">H", len(self.name.encode())) +
                       self.name.encode() +
                       struct.pack(">H", len(raw)) + raw)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone structure chest check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19152)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_chestgen_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=ChestGen\nview-distance=4\ngamemode=1\nworld-seed=1337\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("ChestGen\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 3\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"normal\"\nmemory:\n  max-mb: 512\n"
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
        deadline = time.time() + 120
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

        a = ChestClient(args.host, args.port, "ChestGen", verbose=args.verbose)
        print("stage: handshake+login+spawn")
        if not a.handshake() or not a.login():
            print("FAIL: handshake/login")
            return 1
        a.drain(4.0)
        a.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        a.drain(2.5)
        if a.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1
        print(f"  eid={a.eid}")

        # Sweep a grid of teleport points: every stop makes the server
        # generate + stream a fresh chunk neighborhood, so each stop is a
        # fresh batch of population (dungeons, villages, temples...) and
        # the chunk stream carries its tile blobs.
        steps = [(0, 0), (120, 0), (240, 0), (0, 120), (120, 120),
                 (240, 120), (0, 240), (120, 240), (240, 240),
                 (360, 120), (120, 360), (-120, 120), (120, -120)]
        for sx, sz in steps:
            if proc.poll() is not None:
                break
            a.say(f"/tp {128 + sx} 90 {128 + sz}")
            a.drain(3.0)

        chests = [t for t in a.tiles
                  if isinstance(t[3], dict) and t[3].get("id") == "Chest"]
        loot_entries = []
        for _, _, _, tag in chests:
            items = tag.get("Items")
            if isinstance(items, list):
                loot_entries.extend(e for e in items if isinstance(e, dict))
        loaded = [t for t in chests
                  if isinstance(t[3].get("Items"), list) and t[3]["Items"]]

        check(proc.poll() is None, "server survives the full sweep")
        check(len(chests) > 0, "chest tiles stream with the chunks",
              f"tiles={len(a.tiles)}")
        check(len(loaded) > 0, "generated chests arrive WITH loot",
              f"chests={len(chests)} loaded={len(loaded)}")

        # Every item entry must be a well-formed slot entry.
        bad = []
        for e in loot_entries:
            slot = e.get("Slot", -1)
            iid = e.get("id", 0)
            count = e.get("Count", 0)
            if not (0 <= slot <= 26) or not (iid > 0) or not (count > 0):
                bad.append(e)
        check(not bad, "loot entries are valid (Slot 0..26, id>0, Count>0)",
              f"bad={bad[:3]}")

        kinds = {e.get("id") for e in loot_entries}
        check(len(kinds) >= 3, "loot spans several item types (a pool)",
              f"kinds={sorted(kinds)}")

        print(f"  sweep: tiles={len(a.tiles)} chests={len(chests)} "
              f"with-loot={len(loaded)} item-kinds={len(kinds)}")
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()

    print("RESULT:", "PASS" if not failures else f"FAIL ({len(failures)})")
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
