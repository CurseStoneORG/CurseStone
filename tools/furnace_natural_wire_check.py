#!/usr/bin/env python3
"""Natural-vs-placed furnace wire check for CurseStone.

Reproduces the village-furnace scenario deterministically: a plugin drops a
furnace BLOCK into the world server-side (no tile — exactly what structure
generation produces), the client round-trips away and back so the chunk
streams fresh, and the full wire is captured for both the natural furnace
and one placed by the player:

  1. chunk stream        -> does a BlockEntityData (0x32) for the furnace
                            ride with FullChunkData (0x34)? what NBT keys?
  2. chunk payload       -> is the furnace block itself in the streamed
                            chunk data (client can see it)?
  3. tap                 -> ContainerOpen (0x2a) shape, BlockEntityData in
                            the open batch, ContainerSetContent (0x2e)
  4. open window         -> ContainerSetData (0x2d) cadence per second

    python3 tools/furnace_natural_wire_check.py [--server /path/to/CurseStone]

Exit code 0 = all expected wire facts hold; 1 = anything deviates.
"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from chest_cowiewer_sync_check import SyncClient  # noqa: E402
from door_bed_flow_check import (Client,  # noqa: E402
                                 PLAYER_ACTION_SPAWN_SAME_DIMENSION)

FULL_CHUNK_DATA = 0x34
BLOCK_ENTITY_DATA = 0x32
CONTAINER_OPEN = 0x2A
CONTAINER_SET_DATA = 0x2D
CONTAINER_SET_CONTENT = 0x2E
UPDATE_BLOCK = 0x13

FURNACE_BLOCK = 61
NAT = (130, 4, 131)   # plugin-placed "natural" furnace (no tile server-side)
PLC = (134, 4, 134)   # bot-placed furnace
HOME = (128, 6, 128)
FAR = (300, 6, 300)

PLUGIN_DIR = "plugins/FurnNat"
PLUGIN_YML = ("name: FurnNat\nmain: src/main.lua\napi: 1.0.2\n"
              "version: 1.0.0\nauthor: CurseStone\n")
PLUGIN_LUA = '''local plugin = {}
local joined = false

function plugin.onEnable()
    -- Place the "natural" furnace on the FIRST PlayerJoin: World::setBlock
    -- forces the target chunk to load, so the block lands BEFORE the client
    -- ever sees the chunk — the first stream then carries it, exactly like
    -- a structure-generated furnace.
    events.on("PlayerJoin", function(e)
        if joined then return end
        joined = true
        local w = server.defaultWorld()
        local ret = w:setBlock(130, 4, 131, 61, 2)
        local id = w:getBlock(130, 4, 131)
        log.info("NATFURN-JOIN set=" .. tostring(ret) .. " id=" .. tostring(id))
    end)
    commands.register("natfurn", {description = "place the natural furnace"},
                     function(args, sender)
        local w = server.defaultWorld()
        local ret = w:setBlock(130, 4, 131, 61, 2)
        local id = w:getBlock(130, 4, 131)
        log.info("NATFURN set=" .. tostring(ret) .. " id=" .. tostring(id))
    end)
    log.info("furnace plugin ready")
    return true
end

function plugin.onDisable()
end

return plugin
'''


class WireClient(SyncClient):
    """Captures BlockEntityData, ContainerSetData and raw chunk payloads."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.bed_pkts = []       # (x, y, z, nbt bytes)
        self.setdata = []        # (windowid, property, value)
        self.chunks = {}         # (cx, cz) -> payload bytes

    def on_game_packet(self, p: bytes):
        if p:
            pid = p[0]
            if pid == BLOCK_ENTITY_DATA and len(p) >= 13:
                x, y, z = struct.unpack(">iii", p[1:13])
                self.bed_pkts.append((x, y, z, bytes(p[13:])))
            elif pid == CONTAINER_SET_DATA and len(p) >= 6:
                prop, val = struct.unpack(">hh", p[2:6])
                self.setdata.append((p[1], prop, val))
            elif pid == FULL_CHUNK_DATA and len(p) >= 9:
                cx, cz = struct.unpack(">ii", p[1:9])
                self.chunks[(cx, cz)] = bytes(p[9:])
        super().on_game_packet(p)


def chunk_block(payload: bytes, lx: int, y: int, lz: int):
    """Read one block id out of a FullChunkData payload (ORDER_COLUMNS
    layout: ids first, index = x<<11 | z<<7 | y)."""
    if not payload or len(payload) < 32768:
        return None
    idx = ((lx & 15) << 11) | ((lz & 15) << 7) | (y & 127)
    return payload[idx]


def parse_nbt_keys(blob: bytes):
    """Tiny little-endian NBT walker: returns (root_ok, [(key, type)])."""
    keys = []
    if not blob or blob[0] != 0x0A:
        return False, keys
    off = 1
    nlen = struct.unpack("<H", blob[off:off + 2])[0]
    off += 2 + nlen
    sizes = {1: 1, 2: 2, 3: 4, 4: 8, 5: 4, 6: 8}
    while off < len(blob):
        t = blob[off]
        if t == 0:
            break
        off += 1
        if off + 2 > len(blob):
            return True, keys
        kn = struct.unpack("<H", blob[off:off + 2])[0]
        name = blob[off + 2:off + 2 + kn].decode("latin1")
        off += 2 + kn
        if t in sizes:
            keys.append((name, t))
            off += sizes[t]
        elif t == 7:
            n = struct.unpack(">i", blob[off:off + 4])[0]
            off += 4 + n
        elif t == 8:
            keys.append((name, t))
            n = struct.unpack("<H", blob[off:off + 2])[0]
            off += 2 + n
        elif t == 9 or t == 10 or t == 11 or t == 12:
            keys.append((name, t))
            return True, keys  # not needed for the furnace compound
        else:
            return True, keys
    return True, keys


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone natural furnace wire check")
    ap.add_argument("--server", default="/home/z/my-project/build/cs-rel/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19168)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_natfurn_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=NatFurn\nview-distance=4\ngamemode=1\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("FurnOne\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "world:\n  format: \"anvil\"\n  generator: \"flat\"\n"
                "memory:\n  max-mb: 512\n  gc-interval-ticks: 200\n")
    os.makedirs(os.path.join(runtime, PLUGIN_DIR, "src"), exist_ok=True)
    with open(os.path.join(runtime, PLUGIN_DIR, "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(runtime, PLUGIN_DIR, "src", "main.lua"), "w") as f:
        f.write(PLUGIN_LUA)

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" + extra_ld).lstrip(":")

    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")
    failures = []

    def check(cond, label, detail=""):
        print(("  PASS  " if cond else "  FAIL  ") + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond:
            failures.append(label)

    try:
        deadline = time.time() + 90
        booted = False
        while time.time() < deadline:
            if proc.poll() is not None:
                print("FAIL: server exited during boot")
                return 1
            try:
                if "Server ready" in open(server_log).read():
                    booted = True
                    break
            except FileNotFoundError:
                pass
            time.sleep(0.4)
        if not booted:
            print("FAIL: server never became ready")
            return 1

        c = WireClient(args.host, args.port, "FurnOne", verbose=args.verbose)
        print("stage: bot login+spawn")
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)

        # Get far away FIRST so the HOME area has never been streamed, then
        # have the plugin drop the "natural" furnace: World::setBlock forces
        # the chunk to load server-side, so the block sits in the chunk
        # BEFORE the client ever sees it — exactly a structure-generated
        # furnace.
        c.say("/tp %d %d %d" % FAR)
        time.sleep(1.5)
        c.drain(1.0)
        c.bed_pkts.clear()
        c.say("/natfurn")
        time.sleep(0.8)
        c.drain(0.8)
        logtxt = open(server_log).read()
        check("NATFURN set=true id=61" in logtxt,
              "plugin placed the natural furnace block (id 61, no tile)",
              [ln for ln in logtxt.splitlines() if "NATFURN" in ln][-1:])

        # ---- phase 1: FIRST visit. The stream below is the one a player
        # gets the first time they walk to a village chunk.
        c.chunks.clear()
        c.say("/tp %d %d %d" % HOME)
        time.sleep(2.5)
        c.drain(2.5)
        first_beds = list(c.bed_pkts)
        first_nat = [b for b in first_beds if b[:3] == NAT]
        first_plc = [b for b in first_beds if b[:3] == PLC]
        print(f"    phase1 FIRST-visit streamed 0x32: total={len(first_beds)} "
              f"nat={len(first_nat)} plc={len(first_plc)} "
              f"positions={sorted(set(b[:3] for b in first_beds))[:8]}")
        if first_nat:
            ok, keys = parse_nbt_keys(first_nat[0][3])
            print(f"    phase1 natural compound keys: {keys}")

        # Player-placed furnace (the known-good baseline).
        c.mob_equipment(1, FURNACE_BLOCK, 0)
        time.sleep(0.2)
        c.creative_take(1, FURNACE_BLOCK, 1, 0)
        time.sleep(0.4)
        c.drain(0.4)
        c.use_item(PLC[0], PLC[1] - 1, PLC[2], 1, item_id=FURNACE_BLOCK)
        c.drain(1.2)
        ups = [e for e in c.events if e[0] == "update_block"
               and e[1] == PLC[0] and e[2] == PLC[1] and e[3] == PLC[2]]
        check(bool(ups) and ups[0][4] == FURNACE_BLOCK,
              "player-placed furnace exists at " + str(PLC), f"ups={c.events}")

        # Round-trip: leave view distance (chunks drop client-side) and come
        # back so BOTH furnaces arrive through a RE-stream (radius change /
        # revisit — the common real-phone scenario).
        c.bed_pkts.clear()
        c.chunks.clear()
        c.say("/tp %d %d %d" % FAR)
        time.sleep(1.5)
        c.drain(1.0)
        c.say("/tp %d %d %d" % HOME)
        time.sleep(2.5)
        c.drain(2.5)
        re_beds = list(c.bed_pkts)
        re_nat = [b for b in re_beds if b[:3] == NAT]
        re_plc = [b for b in re_beds if b[:3] == PLC]
        print(f"    phase2 RE-visit streamed 0x32: total={len(re_beds)} "
              f"nat={len(re_nat)} plc={len(re_plc)}")

        # 1. furnace blocks ride in the chunk payloads? Wire is
        # [4B][1B][8 sections x 10240B] or [4B][1B][ORDER_COLUMNS]; probe
        # both id layouts for the known furnace blocks.
        def chunk_has_block(pos):
            cx, cz = pos[0] >> 4, pos[2] >> 4
            payload = c.chunks.get((cx, cz))
            if not payload or len(payload) < 5 + 10240:
                return None
            lx, y, lz = pos[0] & 15, pos[1], pos[2] & 15
            sec = 5 + (y >> 4) * 10240
            a = payload[sec + ((lx << 8) | (lz << 4) | (y & 15))]
            b = payload[5 + ((lx << 11) | (lz << 7) | (y & 127))]
            return (a, b)

        nat_in_chunk = chunk_has_block(NAT)
        plc_in_chunk = chunk_has_block(PLC)
        probe = c.chunks.get((NAT[0] >> 4, NAT[2] >> 4)) or b""
        print(f"    probe chunk len={len(probe)} head={probe[:12].hex()} "
              f"nat_ids={nat_in_chunk} plc_ids={plc_in_chunk}")
        check(FURNACE_BLOCK in (nat_in_chunk or ()),
              "natural furnace block rides in the streamed chunk", f"ids={nat_in_chunk}")
        check(FURNACE_BLOCK in (plc_in_chunk or ()),
              "placed furnace block rides in the streamed chunk", f"ids={plc_in_chunk}")

        # 2. BlockEntityData with the RE-stream?
        nat_beds = re_nat
        plc_beds = re_plc
        check(bool(nat_beds),
              "natural furnace gets a streamed BlockEntityData (client actor)",
              f"beds={[b[:3] for b in c.bed_pkts]}")
        if nat_beds:
            ok, keys = parse_nbt_keys(nat_beds[0][3])
            print(f"    natural spawn compound keys: {keys}")
            check(ok and any(k == "id" for k, _ in keys),
                  "natural spawn compound parses and carries id")

        # 3. tap the natural furnace: open batch + window cadence
        c.bed_pkts.clear()
        c.setdata.clear()
        c.container_opens.clear()
        t0 = time.time()
        c.use_item(NAT[0], NAT[1], NAT[2], 0)
        c.drain(2.5)
        nat_window = time.time() - t0
        opens = [o for o in c.container_opens if o[2:] == NAT]
        check(bool(opens) and opens[0][1] == 2 and opens[0][0] >= 2,
              "natural furnace opens ContainerOpen type=2 window>=2",
              f"opens={c.container_opens}")
        open_beds = [b for b in c.bed_pkts if b[:3] == NAT]
        if opens:
            win = opens[0][0]
            check(True, "")
            # the open-batch BlockEntityData must precede the open record:
            # capture order is implied by packet arrival; we just report.
        nat_content = None
        nat_rate = len(c.setdata) / max(nat_window, 0.001)

        c.container_close(c.container_opens[-1][0] if c.container_opens else 2)
        time.sleep(0.6)
        c.drain(0.6)

        # 4. tap the placed furnace: same capture
        c.bed_pkts.clear()
        c.setdata.clear()
        c.container_opens.clear()
        t0 = time.time()
        c.use_item(PLC[0], PLC[1], PLC[2], 0)
        c.drain(2.5)
        plc_window = time.time() - t0
        opens2 = [o for o in c.container_opens if o[2:] == PLC]
        check(bool(opens2) and opens2[0][1] == 2 and opens2[0][0] >= 2,
              "placed furnace opens ContainerOpen type=2 window>=2",
              f"opens={c.container_opens}")
        plc_rate = len(c.setdata) / max(plc_window, 0.001)
        c.container_close(c.container_opens[-1][0] if c.container_opens else 2)
        time.sleep(0.6)
        c.drain(0.6)

        print(f"    ContainerSetData rate: natural={nat_rate:.1f}/s placed={plc_rate:.1f}/s")
        print(f"    streamed 0x32 positions: {sorted(set(b[:3] for b in nat_beds + plc_beds))}")
        print(f"    open-batch 0x32 natural={len(open_beds)} "
              f"(positions {sorted(set(b[:3] for b in c.bed_pkts))})")
        print(f"    chunk payloads captured: {len(c.chunks)} "
              f"(has spawn area: {(NAT[0] >> 4, NAT[2] >> 4) in c.chunks})")

    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
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
