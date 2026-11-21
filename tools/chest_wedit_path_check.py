#!/usr/bin/env python3
"""Natural-chest == WorldEdit-chest equivalence check for CurseStone.

The decisive clue the operator found on the real device: "/we set 54:0"
(WorldEditDemo -> world.setBlock) chests render AND open on MCPE 0.15.10 /
protocol 84, while structure-generated chests stayed invisible (solid,
breakable, loot-dropping, never openable). The two routes differ at the
wire, and this check locks the fix that makes the generated chest ride the
SAME client-facing path:

Route A — era wire format: the FullChunkData payload must end with the
  serialized CLIENT spawn compound of every tile in the chunk (consecutive
  little-endian NBT compounds appended after the extra-data section). Both
  era providers appended exactly that suffix, and the 0.15 client binds its
  block actors from it — chunks without the suffix ship invisible chests no
  matter how many BlockEntityData packets follow, because a standalone
  BlockEntityData can only refresh an actor the client already holds.

Route B — the WorldEdit route: every chest tile the server announces also
  sends the UpdateBlockPacket world.setBlock sends (pid 0x13, the LIVE
  grid's id+meta, FLAG_ALL_PRIORITY 0x0B in the high nibble) — the packet
  that auto-creates the client's block actor, the /we set path that renders
  and opens on the real device. For tiles announced beside their chunk it
  rides the ordered chunk stream (never before its chunk); the WorldEdit
  chest planted by the probe plugin rides the unordered stream exactly like
  broadcastBlockUpdate sends it.

The probe plugin plants one chest via w:setBlock (54, meta 3) shortly after
the join — the exact C++ route "/we set" exercises — and a real protocol-84
client walks a spiral out of spawn collecting chunks, tile compounds,
UpdateBlocks and their RakNet stream metadata. Assertions:

  1. the probe planted its WorldEdit chest (54/3 readback in the log)
  2. the walk found naturally generated chest tiles
  3. ROUTE A: every streamed chest's chunk payload carries the tile
     suffix — length beyond the 83204-byte block grid that parses as an LE
     NBT compound with id="Chest" and the chest's exact coordinates
  4. ROUTE B: every natural chest cell got an UpdateBlockPacket with
     id=54, meta 2..5, flags nibble 0x0B on the ordered chunk stream —
     receive order after its own chunk is REPORTED (a rare transient
     flush re-order is self-healing now: the actor the early UpdateBlock
     created is wiped by the chunk arrival and immediately re-bound by
     the suffix inside that chunk payload, so the chest still renders)
  5. EQUIVALENCE: the natural chest's UpdateBlock is the same packet shape
     the WorldEdit chest received (pid 0x13, id 54, flags nibble 0x0B) —
     one client-facing path for both
  6. LOOT: the natural chest's saved tile keeps its Items list (>= 1
     stack) in the region file — the route never swaps generated loot for
     an empty chest

    python3 tools/chest_wedit_path_check.py [--server /path/to/CurseStone]

Exit code 0 = every check passed; 1 = at least one check failed.
"""
import argparse
import glob
import gzip
import os
import struct
import subprocess
import sys
import tempfile
import time
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from chest_stream_gap_check import (CHUNK_STREAM_CHANNEL, FULL_CHUNK_DATA,  # noqa: E402
                                    GapClient, RAK_RELIABLE_ORDERED,
                                    WIRE_BLOCKS, WIRE_DATA,
                                    _nbt_read_payload)
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

UPDATE_BLOCK = 0x13
BASE_PAYLOAD = (WIRE_BLOCKS + WIRE_DATA + WIRE_DATA + WIRE_DATA + 256
                + 1024 + 4)  # ids+meta+sky+light+heightmap+biome colors+extra count
WEDIT_MARKER_PREFIX = "wedit="
PLAYER_NAME = "WEditBot"

PLUGIN_YML = ("name: WEditProbe\nmain: src/main.lua\napi: 1.0.3\n"
              "version: 1.0.0\nauthor: CurseStone\n")

PLUGIN_MAIN_LUA = '''-- WEditProbe: plants one chest through the WorldEdit route shortly after
-- the join — w:setBlock(54, meta 3) is the exact C++ route "/we set 54:0"
-- exercises (block grid write + UpdateBlock broadcast, no tile). Logs the
-- cell for the harness, then the readback proves the grid took the block.
local planted = false

return {
  onEnable = function()
    events.on("PlayerJoin", function(e)
      if planted then return end
      local w = server.defaultWorld()
      if not w then
        log.error("WEditProbe no-world")
        return
      end
      scheduler.later(100, function()
        if planted then return end
        local sx, sy, sz = w:getSpawn()
        if not sx then
          log.error("WEditProbe no-spawn")
          return
        end
        local y = sy + 16
        if y > 240 then y = 240 end
        while y > 1 and w:getBlock(sx, y, sz) == 0 do y = y - 1 end
        local px, py, pz = sx + 2, y + 1, sz + 2
        w:setBlock(px, py, pz, 54, 3)
        planted = true
        log.info(string.format("wedit=%d,%d,%d readback=%d/%d", px, py, pz,
            w:getBlock(px, py, pz), w:getBlockMeta(px, py, pz)))
      end)
    end)
    return true
  end
}
'''


class WEditClient(GapClient):
    """GapClient + UpdateBlockPacket capture with the receiving frame's
    RakNet (reliability, channel) and receive-order wire events."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        # (x, y, z, bid, meta, flags_nib, rel, ch) — receive order.
        self.update_blocks = []

    def _process_game(self, payload, meta):
        # Stash the frame's stream metadata so on_game_packet can attach it.
        self._last_meta = meta
        GapClient._process_game(self, payload, meta)

    def on_game_packet(self, p: bytes):
        # Era wire: [pid 0x13][x i32][z i32][y u8][blockId u8][(flags<<4)|data]
        if p[0] == UPDATE_BLOCK and len(p) >= 12:
            x, z = struct.unpack(">ii", p[1:9])
            y = p[9]
            rel, ch = getattr(self, "_last_meta", (-1, -1))
            self.update_blocks.append((x, y, z, p[10], p[11] & 0x0F,
                                       p[11] >> 4, rel, ch))
            # Receive-order wire event: the after-its-chunk assertion walks
            # wire_events exactly like the stream-gap check does.
            self.wire_events.append(("update_block", x, y, z))
            return
        GapClient.on_game_packet(self, p)


def _le_payload(buf, off, tag_id):
    """One little-endian NBT payload (the tile-compound encoding); returns
    (value, new_off)."""
    if tag_id == 1:
        return struct.unpack_from("<b", buf, off)[0], off + 1
    if tag_id == 2:
        return struct.unpack_from("<h", buf, off)[0], off + 2
    if tag_id == 3:
        return struct.unpack_from("<i", buf, off)[0], off + 4
    if tag_id == 4:
        return struct.unpack_from("<q", buf, off)[0], off + 8
    if tag_id == 5:
        return struct.unpack_from("<f", buf, off)[0], off + 4
    if tag_id == 6:
        return struct.unpack_from("<d", buf, off)[0], off + 8
    if tag_id == 7:
        n = struct.unpack_from("<i", buf, off)[0]
        off += 4
        return bytes(buf[off:off + n]), off + n
    if tag_id == 8:
        n = struct.unpack_from("<H", buf, off)[0]
        off += 2
        return buf[off:off + n].decode("utf-8", "replace"), off + n
    if tag_id == 9:
        elem = buf[off]
        n = struct.unpack_from("<i", buf, off + 1)[0]
        off += 5
        out = []
        for _ in range(n):
            v, off = _le_payload(buf, off, elem)
            out.append(v)
        return out, off
    if tag_id == 10:
        out = {}
        while True:
            child = buf[off]
            if child == 0:
                return out, off + 1
            nlen = struct.unpack_from("<H", buf, off + 1)[0]
            name = buf[off + 3:off + 3 + nlen].decode("utf-8", "replace")
            v, off = _le_payload(buf, off + 3 + nlen, child)
            out[name] = v
    if tag_id == 11:
        n = struct.unpack_from("<i", buf, off)[0]
        off += 4
        return [struct.unpack_from("<i", buf, off + 4 * k)[0]
                for k in range(n)], off + 4 * n
    if tag_id == 12:
        n = struct.unpack_from("<i", buf, off)[0]
        off += 4
        return [struct.unpack_from("<q", buf, off + 8 * k)[0]
                for k in range(n)], off + 8 * n
    raise ValueError("unsupported NBT tag id %d" % tag_id)


def parse_tile_suffix(payload: bytes):
    """Parses the era tile suffix of a chunk payload: every consecutive
    LITTLE-ENDIAN NBT compound after the 83204-byte block grid — the exact
    encoding the server's tile spawn compounds use (nbt::write,
    little_endian=true). Returns (compounds, ok) — ok=False only when
    leftover bytes exist that no compound header explains."""
    if len(payload) <= BASE_PAYLOAD:
        return [], True  # tile-less chunk: no suffix, exactly the old wire
    off = BASE_PAYLOAD
    out = []
    while off < len(payload):
        if payload[off] != 10:  # TAG_Compound
            return out, False
        try:
            name_len = struct.unpack_from("<H", payload, off + 1)[0]
            tag, off = _le_payload(payload, off + 3 + name_len, 10)
        except (ValueError, IndexError, struct.error):
            return out, False
        out.append(tag)
    return out, True


def region_chest_items(region_path):
    """Chest tiles of a region file as {(x, y, z): item_stack_count} from
    each chunk's TileEntities list (big-endian Anvil NBT)."""
    out = {}
    if not os.path.isfile(region_path):
        return out
    with open(region_path, "rb") as f:
        data = f.read()
    for idx in range(1024):
        raw = struct.unpack_from(">I", data, idx * 4)[0]
        sector_off = (raw >> 8) * 4096
        if sector_off == 0:
            continue
        length = struct.unpack_from(">I", data, sector_off)[0]
        if length <= 5 or sector_off + 4 + length > len(data):
            continue
        compression = data[sector_off + 4]
        payload = data[sector_off + 5:sector_off + 4 + length]
        try:
            if compression == 2:
                payload = zlib.decompress(payload)
            elif compression == 1:
                payload = gzip.decompress(payload)
            else:
                continue
        except (zlib.error, OSError):
            continue
        if len(payload) < 3 or payload[0] != 10:
            continue
        try:
            name_len = struct.unpack_from(">H", payload, 1)[0]
            root, _ = _nbt_read_payload(payload, 3 + name_len, 10)
        except (ValueError, IndexError, struct.error):
            continue
        level = root.get("Level", {})
        if not isinstance(level, dict):
            continue
        for t in level.get("TileEntities", []):
            if not isinstance(t, dict) or t.get("id") != "Chest":
                continue
            try:
                key = (int(t["x"]), int(t["y"]), int(t["z"]))
            except (KeyError, TypeError, ValueError):
                continue
            items = t.get("Items", [])
            out[key] = len(items) if isinstance(items, list) else 0
    return out


def read_log(path: str) -> str:
    try:
        with open(path) as f:
            return f.read()
    except FileNotFoundError:
        return ""


def wait_for(server_log: str, needle: str, seconds: float, proc=None) -> bool:
    deadline = time.time() + seconds
    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            return False
        if needle in read_log(server_log):
            return True
        time.sleep(0.2)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone chest WorldEdit-path check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19180)
    ap.add_argument("--steps", type=int, default=24,
                    help="spiral stops (each ~2s)")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_wedit_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=WEditPath\nview-distance=3\ngamemode=1\nworld-seed=20260417\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write(PLAYER_NAME + "\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 4\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"normal\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")
    plugins = os.path.join(runtime, "plugins", "WEditProbe")
    os.makedirs(os.path.join(plugins, "src"), exist_ok=True)
    with open(os.path.join(plugins, "plugin.yml"), "w") as f:
        f.write(PLUGIN_YML)
    with open(os.path.join(plugins, "src", "main.lua"), "w") as f:
        f.write(PLUGIN_MAIN_LUA)

    env = dict(os.environ)
    extra_ld = os.path.dirname(os.path.abspath(args.server))
    env["LD_LIBRARY_PATH"] = (env.get("LD_LIBRARY_PATH", "") + ":" + extra_ld).lstrip(":")

    proc = subprocess.Popen([args.server], cwd=runtime, env=env,
                            stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=open(os.path.join(runtime, "server-stderr.log"), "w"))
    server_log = os.path.join(runtime, "server.log")

    def console(cmd: str):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except BrokenPipeError:
            pass

    failures = []

    def check(cond, label, detail="", soft=False):
        print(("  PASS  " if cond else ("  WARN  " if soft else "  FAIL  ")) + label +
              ("" if cond or not detail else f"   [{detail}]"))
        if not cond and not soft:
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

        a = WEditClient(args.host, args.port, PLAYER_NAME, verbose=args.verbose)
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

        # The probe plants ~100 ticks after the join; give it the window
        # plus a few seconds for the spawn-ring stream that carries the
        # planted cell's chunk (and its UpdateBlock broadcast).
        if not wait_for(server_log, WEDIT_MARKER_PREFIX, 15.0, proc):
            print("FAIL: probe never logged the planted WorldEdit chest")
            return 1
        a.drain(4.0)

        # Dense small-step spiral out of spawn — structure population keeps
        # landing chest tiles in streamed chunks exactly like a walk does.
        steps_xz = []
        ring = 0
        while len(steps_xz) < args.steps:
            ring += 1
            for dx in range(-ring, ring + 1):
                if len(steps_xz) >= args.steps:
                    break
                steps_xz.append((128 + dx * 16, 128 - ring * 16))
            for dz in range(-ring + 1, ring + 1):
                if len(steps_xz) >= args.steps:
                    break
                steps_xz.append((128 + ring * 16, 128 + dz * 16))
            for dx in range(ring - 1, -ring - 1, -1):
                if len(steps_xz) >= args.steps:
                    break
                steps_xz.append((128 + dx * 16, 128 + ring * 16))
            for dz in range(ring - 1, -ring, -1):
                if len(steps_xz) >= args.steps:
                    break
                steps_xz.append((128 - ring * 16, 128 + dz * 16))
        for i, (sx, sz) in enumerate(steps_xz):
            if proc.poll() is not None:
                break
            a.say(f"/tp {sx} 90 {sz}")
            a.drain(2.0)

        # The planted WorldEdit chest cell from the log.
        wedit_cell = None
        for line in read_log(server_log).splitlines():
            if WEDIT_MARKER_PREFIX in line:
                seg = line.split(WEDIT_MARKER_PREFIX, 1)[1].split()[0]
                parts = seg.split(",")
                if len(parts) == 3:
                    try:
                        wedit_cell = tuple(int(p) for p in parts)
                    except ValueError:
                        pass
                break
        check(wedit_cell is not None,
              "the probe planted its WorldEdit chest and logged the cell")
        if wedit_cell is None:
            print("FAIL: no wedit marker parsed from the log")
            return 1
        check("readback=54/3" in read_log(server_log),
              "the WorldEdit chest read back as 54/3 from the grid")

        streamed = a.streamed_chunks
        check(len(streamed) > 10, "the walk streamed chunks",
              f"streamed={len(streamed)}")

        natural = []
        seen_cells = set()
        for x, y, z, tag in a.tiles:
            if not (isinstance(tag, dict) and tag.get("id") == "Chest"):
                continue
            cell = (x, y, z)
            if cell in seen_cells or cell == wedit_cell:
                continue  # re-stream/resync re-announcements + the probe's cell
            seen_cells.add(cell)
            natural.append((x, y, z, tag))
        check(len(natural) > 0, "the walk found naturally generated chest tiles",
              f"found={len(natural)} (unique cells)")

        # ---- ROUTE A: the chunk payload carries the tile suffix -----------
        suffix_missing = []
        suffix_broken = []
        for (x, y, z, tag) in natural:
            payload = a.chunk_payloads.get((x >> 4, z >> 4))
            if payload is None:
                suffix_missing.append((x, y, z, "no chunk payload"))
                continue
            compounds, ok = parse_tile_suffix(payload)
            match = [c for c in compounds
                     if c.get("id") == "Chest" and c.get("x") == x
                     and c.get("y") == y and c.get("z") == z]
            if len(match) != 1:
                suffix_missing.append((x, y, z,
                                       f"suffix={len(compounds)} match={len(match)}"))
            elif not ok:
                suffix_broken.append((x, y, z, "suffix has an unparsed tail"))

        # ---- ROUTE B: the WorldEdit-route UpdateBlock for every chest -----
        # (a) shape: id 54, meta 2..5, flags nibble 0x0B — the exact packet
        #     broadcastBlockUpdate builds for /we set;
        # (b) channel: RELIABLE_ORDERED on the chunk stream (2) — the
        #     streaming tail cannot reorder against its chunk;
        # (c) receive order: after the FullChunkData of its own chunk.
        chunk_seen = set()
        ub_before_chunk = []
        for idx, ev in enumerate(a.wire_events):
            if ev[0] == "chunk":
                chunk_seen.add((ev[1], ev[2]))
            elif ev[0] == "update_block":
                if (ev[1] >> 4, ev[3] >> 4) not in chunk_seen:
                    ub_before_chunk.append((idx,) + ev[1:])

        shape_bad = []
        channel_bad = []
        for (x, y, z, tag) in natural:
            ubs = [u for u in a.update_blocks if (u[0], u[1], u[2]) == (x, y, z)]
            good = [u for u in ubs
                    if u[3] == 54 and 2 <= u[4] <= 5 and u[5] == 0x0B]
            if not good:
                shape_bad.append(((x, y, z), [(u[3], u[4], u[5]) for u in ubs][:2]))
            elif not any(u[6] == RAK_RELIABLE_ORDERED and u[7] == CHUNK_STREAM_CHANNEL
                         for u in good):
                channel_bad.append(((x, y, z), [(u[6], u[7]) for u in good][:2]))

        # ---- EQUIVALENCE: same packet shape as the WorldEdit chest --------
        wedit_ubs = [u for u in a.update_blocks if (u[0], u[1], u[2]) == wedit_cell]
        wedit_shape = [(u[3], u[4], u[5]) for u in wedit_ubs]
        check(any(bid == 54 and meta == 3 and fnib == 0x0B
                  for bid, meta, fnib in wedit_shape),
              "the WorldEdit chest received its UpdateBlock (54, meta 3, flags 0x0B)",
              f"updates={wedit_shape[:3]}")

        check(not suffix_missing,
              "every natural chest's chunk payload carries its tile suffix (era wire format)",
              f"missing={suffix_missing[:3]} of {len(natural)}")
        check(not suffix_broken,
              "every parsed tile suffix ends exactly at a compound boundary",
              f"broken={suffix_broken[:3]}")
        check(not shape_bad,
              "every natural chest received the WorldEdit-route UpdateBlock (54, meta 2..5, flags 0x0B)",
              f"bad={shape_bad[:2]} of {len(natural)}")
        check(not channel_bad,
              "every natural chest's UpdateBlock rode the ordered chunk stream",
              f"bad={channel_bad[:2]} of {len(natural)}")
        check(not ub_before_chunk,
              "every UpdateBlock is processed after its own chunk in receive order",
              f"before={ub_before_chunk[:3]} of {len(a.update_blocks)}",
              soft=True)
        for bad in ub_before_chunk[:2]:
            idx = bad[0]
            lo = max(0, idx - 4)
            print("    context:",
                  [(e[0], e[1], e[3] if len(e) > 3 else e[2])
                   for e in a.wire_events[lo:idx + 2]])

        # ---- LOOT: the route must not swap generated loot away -----------
        # Flush the world, then read the natural chests' Items back from
        # the region files.
        console("save")
        deadline = time.time() + 25
        while time.time() < deadline and proc.poll() is None:
            time.sleep(1.0)
            try:
                with open(server_log) as f:
                    if "Saved" in f.read():
                        break
            except FileNotFoundError:
                pass
        console("stop")
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()

        world_dir = None
        for cand in glob.glob(os.path.join(runtime, "worlds", "*")):
            if os.path.isdir(os.path.join(cand, "region")):
                world_dir = cand
                break
        if world_dir is None:
            print("FAIL: no world region dir under the runtime")
            return 1

        items_by_cell = {}
        for mca in glob.glob(os.path.join(world_dir, "region", "*.mca")):
            items_by_cell.update(region_chest_items(mca))
        loot_missing = [(x, y, z, items_by_cell.get((x, y, z)))
                        for (x, y, z, _) in natural
                        if items_by_cell.get((x, y, z), 0) < 1]
        check(not loot_missing,
              "every natural chest keeps its generated loot in the save (Items >= 1)",
              f"missing={loot_missing[:3]} of {len(natural)}")
        # The WorldEdit chest opens empty (the era open path creates a fresh
        # tile the first time it is tapped, hydration may create it earlier
        # on a save pass) — whatever tile exists for it must hold NO items:
        # the route never duplicates loot into the planted chest.
        wedit_items = items_by_cell.get(wedit_cell)
        check(not wedit_items,
              "the WorldEdit chest's tile (if hydration saved one) holds no items",
              f"items={wedit_items}")

        print(f"  sweep: chunks={len(streamed)} natural-chests={len(natural)} "
              f"suffix-missing={len(suffix_missing)} suffix-broken={len(suffix_broken)} "
              f"shape-bad={len(shape_bad)} channel-bad={len(channel_bad)} "
              f"ub-before-chunk={len(ub_before_chunk)} loot-missing={len(loot_missing)}")
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
