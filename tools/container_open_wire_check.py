#!/usr/bin/env python3
"""End-to-end furnace/chest container open byte check for CurseStone.

The real-client crash: tapping a village furnace closed the whole game
right after "[FURNACE] ... opened furnace". The fake e2e clients never
validate the open sequence, so the wire had to be captured and checked
against the era-84 reference (PocketMine-MP 1.6dev):

  reference open flow for a block container:
    ContainerOpen  = windowid u8, type u8, slots i16, x/y/z i32 (16 B,
                     NO trailing entityId long)
    then ContainerSetContent with the slot dump
    BlockEntityData belongs BEFORE the open (the client binds the
    furnace UI to the block actor at those coords; a furnace screen
    without one dies, a chest just refuses)
    no ContainerSetData until the tile actually ticks progress

A real protocol-84 client drives a self-managed flat server:

  1. place a furnace + chest at negative coords (village-like)
  2. restart the server so the tiles reload from disk, relogin and
     capture the chunk-stream BlockEntityData (the hydrate path)
  3. tap the furnace, capture the open sequence and assert:
       - a BlockEntityData with a parseable {"id":"Furnace"} NBT
         arrives BEFORE ContainerOpen
       - ContainerOpen is exactly 16 bytes (type 2, slots 3, right
         coords, no trailing 8-byte entityId garbage)
       - ContainerSetContent follows with 3 empty slots
       - no ContainerSetData squeezed between the open packets
  4. same order checks for the chest (type 0)

    python3 tools/container_open_wire_check.py [--server /path/to/CurseStone]

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
from chest_cowiewer_sync_check import SyncClient  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

# Protocol-84 packet ids (PocketMine-MP 1.6dev Info.php) — NOT the later
# 0.16/1.x renumbering.
CONTAINER_OPEN = 0x2A
CONTAINER_CLOSE = 0x2B
CONTAINER_SET_CONTENT = 0x2E
CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_DATA = 0x2D
BLOCK_ENTITY_DATA = 0x32
UPDATE_BLOCK = 0x13

FURNACE_X, FURNACE_Y, FURNACE_Z = -4, 4, -4  # placed block (click face top of y=3)
CHEST_X, CHEST_Y, CHEST_Z = -4, 4, -2
TP_X, TP_Y, TP_Z = -8, 6, -8
FURNACE_BLOCK = 61
CHEST_BLOCK = 54


# ---- minimal little-endian NBT reader (enough for block entities) ----
def nbt_read_string(b, i):
    (n,) = struct.unpack_from("<H", b, i)
    i += 2
    return b[i:i + n].decode("utf-8", "replace"), i + n


def nbt_skip_payload(b, i, t):
    if t == 0:
        return i
    sizes = {1: 1, 2: 2, 3: 4, 4: 8, 5: 4, 6: 8}
    if t in sizes:
        return i + sizes[t]
    if t == 7:  # byte array
        (n,) = struct.unpack_from("<i", b, i)
        return i + 4 + n
    if t == 8:  # string
        _, i = nbt_read_string(b, i)
        return i
    if t == 9:  # list
        et = b[i]
        (n,) = struct.unpack_from("<i", b, i + 1)
        i += 5
        for _ in range(max(0, n)):
            i = nbt_skip_payload(b, i, et)
        return i
    if t == 10:  # compound
        while True:
            ct = b[i]
            i += 1
            if ct == 0:
                return i
            _, i = nbt_read_string(b, i)
            i = nbt_skip_payload(b, i, ct)
    if t == 11:  # int array
        (n,) = struct.unpack_from("<i", b, i)
        return i + 4 + 4 * n
    raise ValueError(f"bad tag type {t}")


def nbt_parse_compound(blob):
    """Parses a LE named-compound blob into {name: value}; values are
    plain python types (compounds -> dict, lists -> (elem_type, [vals]))."""
    t = blob[0]
    if t != 10:
        raise ValueError(f"root is tag {t}, expected compound")
    name, i = nbt_read_string(blob, 1)
    out = {}

    def payload(i2, t2):
        if t2 == 1:
            return blob[i2], i2 + 1
        if t2 == 2:
            v = struct.unpack_from("<h", blob, i2)[0]
            return v, i2 + 2
        if t2 == 3:
            v = struct.unpack_from("<i", blob, i2)[0]
            return v, i2 + 4
        if t2 == 4:
            v = struct.unpack_from("<q", blob, i2)[0]
            return v, i2 + 8
        if t2 == 5:
            v = struct.unpack_from("<f", blob, i2)[0]
            return v, i2 + 4
        if t2 == 6:
            v = struct.unpack_from("<d", blob, i2)[0]
            return v, i2 + 8
        if t2 == 7:
            (n,) = struct.unpack_from("<i", blob, i2)
            return bytes(blob[i2 + 4:i2 + 4 + n]), i2 + 4 + n
        if t2 == 8:
            return nbt_read_string(blob, i2)
        if t2 == 9:
            et = blob[i2]
            (n,) = struct.unpack_from("<i", blob, i2 + 1)
            i2 += 5
            vals = []
            for _ in range(max(0, n)):
                v, i2 = payload(i2, et)
                vals.append(v)
            return (et, vals), i2
        if t2 == 10:
            d = {}
            while True:
                ct = blob[i2]
                i2 += 1
                if ct == 0:
                    return d, i2
                cname, i2 = nbt_read_string(blob, i2)
                v, i2 = payload(i2, ct)
                d[cname] = v
        if t2 == 11:
            (n,) = struct.unpack_from("<i", blob, i2)
            i2 += 4
            vals = []
            for _ in range(max(0, n)):
                v = struct.unpack_from("<i", blob, i2)[0]
                vals.append(v)
                i2 += 4
            return vals, i2
        raise ValueError(f"bad tag type {t2}")

    while True:
        ct = blob[i]
        i += 1
        if ct == 0:
            break
        cname, i = nbt_read_string(blob, i)
        v, i = payload(i, ct)
        out[cname] = v
    return name, out


class WireClient(SyncClient):
    """SyncClient + raw open-sequence capture (open/content/data/bed)."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.raw = []  # (pid, bytes) for the pids we care about

    def on_game_packet(self, p: bytes):
        if p and p[0] in (CONTAINER_OPEN, CONTAINER_SET_CONTENT,
                          CONTAINER_SET_DATA, BLOCK_ENTITY_DATA):
            self.raw.append((p[0], bytes(p)))
            return
        super().on_game_packet(p)

    def clear_raw(self):
        self.raw = []


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone container open wire check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19151)
    ap.add_argument("--name", default="WireTester")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_openwire_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=OpenWire\nview-distance=8\ngamemode=1\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write(args.name + "\n" + args.name + "2\n")
    with open(os.path.join(runtime, "cursestone.yml"), "w") as f:
        f.write("settings:\n  async-workers: 2\n  autosave-ticks: 6000\n"
                "  advanced-log: true\nworld:\n  format: \"anvil\"\n"
                "  generator: \"flat\"\nmemory:\n  max-mb: 512\n"
                "  gc-interval-ticks: 200\n")

    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.dirname(args.server)
    proc = subprocess.Popen(
        [args.server], cwd=runtime, env=env,
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL)
    server_log = os.path.join(runtime, "server.log")

    def console(cmd):
        try:
            proc.stdin.write((cmd + "\n").encode())
            proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass

    failures = []

    def check(cond, label):
        print(("  PASS  " if cond else "  FAIL  ") + label)
        if not cond:
            failures.append(label)

    def boot_and_wait():
        deadline = time.time() + 40
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

    try:
        if not boot_and_wait():
            print("FAIL: server never became ready")
            return 1

        c = WireClient(args.host, args.port, args.name, verbose=args.verbose)
        print("stage: handshake+login + block placement")
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1

        c.clear_raw()
        c.say("/gamemode 1")
        time.sleep(0.8)
        c.drain(0.8)
        c.say(f"/tp {TP_X} {TP_Y} {TP_Z}")
        time.sleep(1.0)
        c.drain(1.0)
        c.mob_equipment(1, FURNACE_BLOCK, 0)
        time.sleep(0.2)
        c.creative_take(1, FURNACE_BLOCK, 1, 0)
        c.mob_equipment(2, CHEST_BLOCK, 0)
        time.sleep(0.2)
        c.creative_take(2, CHEST_BLOCK, 1, 0)
        time.sleep(0.4)
        c.drain(0.4)
        c.use_item(FURNACE_X, FURNACE_Y - 1, FURNACE_Z, 1, item_id=FURNACE_BLOCK)
        c.use_item(CHEST_X, CHEST_Y - 1, CHEST_Z, 1, item_id=CHEST_BLOCK)
        c.drain(2.0)
        ups = [e for e in c.events if e[0] == "update_block"]
        print(f"    debug: update_block events={ups[:12]}")
        furn = [e for e in ups if e[1] == FURNACE_X and e[3] == FURNACE_Z and e[2] == FURNACE_Y]
        chest = [e for e in ups if e[1] == CHEST_X and e[3] == CHEST_Z and e[2] == CHEST_Y]
        check(bool(furn) and furn[-1][4] == FURNACE_BLOCK, "furnace block placed")
        check(bool(chest) and chest[-1][4] == CHEST_BLOCK, "chest block placed")

        # -------- restart: tiles persist through save/load, relogin ------
        print("stage: server restart (tile reload from disk)")
        console("stop")
        try:
            proc.wait(timeout=20)
        except subprocess.TimeoutExpired:
            proc.kill()
            return 1
        print("    first server stopped ok")
        time.sleep(1.0)
        try:
            proc2 = subprocess.Popen(
                [args.server], cwd=runtime, env=env,
                stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL)
        except Exception as exc:
            print(f"FAIL: second server Popen raised: {exc}")
            return 1
        proc.stdin.close()
        proc = proc2
        print("    second server launched, waiting for ready")
        if not boot_and_wait():
            print("FAIL: server never became ready (2nd boot)")
            return 1
        print("    second server ready")
        time.sleep(1.5)

        c2 = None
        for attempt in range(3):
            cand = WireClient(args.host, args.port, args.name + "2",
                              verbose=args.verbose)
            if cand.handshake() and cand.login():
                c2 = cand
                break
            print(f"    login retry {attempt + 1}")
            time.sleep(1.0)
        if c2 is None:
            print("FAIL: handshake/login after restart")
            return 1
        c2.drain(4.0)
        c2.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c2.say("/gamemode 1")
        time.sleep(0.8)
        c2.drain(0.8)
        c2.say(f"/tp {TP_X} {TP_Y} {TP_Z}")
        time.sleep(1.0)
        c2.drain(1.0)

        print("stage: chunk-stream block entity capture")
        stream_be = {}
        deadline = time.time() + 12.0
        while time.time() < deadline:
            c2.drain(0.4)
            for pid, p in c2.raw:
                if pid == BLOCK_ENTITY_DATA and len(p) >= 13:
                    x, y, z = struct.unpack(">iii", p[1:13])
                    stream_be[(x, y, z)] = p[13:]
            c2.clear_raw()
            if (FURNACE_X, FURNACE_Y, FURNACE_Z) in stream_be and \
               (CHEST_X, CHEST_Y, CHEST_Z) in stream_be:
                break
        fb = stream_be.get((FURNACE_X, FURNACE_Y, FURNACE_Z))
        check(fb is not None,
              "furnace BlockEntityData arrives with the chunk stream")
        if fb:
            try:
                _, tags = nbt_parse_compound(fb)
                check(tags.get("id") == "Furnace",
                      f"stream furnace NBT has id=Furnace (fields={sorted(tags)})")
                check("BurnTime" in tags and "CookTime" in tags,
                      "stream furnace NBT carries client keys BurnTime/CookTime")
            except Exception as exc:
                check(False, f"stream furnace NBT parses ({exc})")

        # -------- tap the furnace: capture the open sequence -------------
        print("stage: furnace tap open sequence")
        c2.clear_raw()
        c2.use_item(FURNACE_X, FURNACE_Y, FURNACE_Z, 1, item_id=FURNACE_BLOCK)
        deadline = time.time() + 3.0
        while time.time() < deadline:
            c2.drain(0.25)
            pids = [pid for pid, _ in c2.raw]
            if CONTAINER_OPEN in pids and CONTAINER_SET_CONTENT in pids:
                break
        seq = c2.raw
        seq_pids = [pid for pid, _ in seq]
        print("    open sequence:", [
            {CONTAINER_OPEN: "open", CONTAINER_SET_CONTENT: "content",
             CONTAINER_SET_DATA: "data", BLOCK_ENTITY_DATA: "bed"}.get(x, hex(x))
            for x in seq_pids[:12]])

        open_idx = seq_pids.index(CONTAINER_OPEN) if CONTAINER_OPEN in seq_pids else -1
        check(open_idx >= 0, "furnace ContainerOpen sent")
        be_before = [i for i, (pid, _) in enumerate(seq[:open_idx])
                     if pid == BLOCK_ENTITY_DATA]
        check(bool(be_before),
              "BlockEntityData arrives BEFORE the furnace ContainerOpen")
        if be_before:
            be_bytes = seq[be_before[-1]][1][13:]
            try:
                _, tags = nbt_parse_compound(be_bytes)
                check(tags.get("id") == "Furnace",
                      "pre-open furnace NBT parses with id=Furnace")
                # The 0.15.10 furnace screen binds BurnTime/CookTime when
                # the BlockEntityData lands; a compound without them is a
                # null read inside the client and crashes the game — the
                # village-furnace crash. The disk keys (BurnDuration/
                # BurnTicks/CookTicks) must never leak to the wire.
                check("BurnTime" in tags and "CookTime" in tags,
                      f"pre-open furnace NBT carries client keys "
                      f"BurnTime/CookTime (fields={sorted(tags)})")
                check("BurnDuration" not in tags and "BurnTicks" not in tags,
                      "pre-open furnace NBT has no disk-only keys")
            except Exception as exc:
                check(False, f"pre-open furnace NBT parses ({exc})")

        if open_idx >= 0:
            op = seq[open_idx][1]
            check(len(op) == 17,  # pid byte + 16 payload bytes
                  f"ContainerOpen is 17 bytes total, no trailing entityId "
                  f"(got {len(op)})")
            if len(op) >= 17:
                wid = op[1]
                typ = op[2]
                slots = struct.unpack(">h", op[3:5])[0]
                ox, oy, oz = struct.unpack(">iii", op[5:17])
                check(typ == 2, f"open type=2 furnace (got {typ})")
                check(slots == 3, f"open slots=3 (got {slots})")
                check((ox, oy, oz) == (FURNACE_X, FURNACE_Y, FURNACE_Z),
                      f"open coords match (got {ox},{oy},{oz})")
                check(wid >= 2, f"open windowid>=2 (got {wid})")

        ci = seq_pids.index(CONTAINER_SET_CONTENT) if CONTAINER_SET_CONTENT in seq_pids else -1
        between = [pid for pid, _ in seq[open_idx + 1:ci]] if open_idx >= 0 and ci > open_idx else []
        check(CONTAINER_SET_DATA not in between,
              f"no ContainerSetData squeezed between open and content "
              f"(got {[hex(x) for x in between]})")
        if ci >= 0:
            content = seq[ci][1]
            (count,) = struct.unpack(">H", content[2:4])
            check(count == 3, f"content dump has 3 slots (got {count})")

        # -------- chest: same order guarantees ---------------------------
        print("stage: chest tap open sequence")
        c2.clear_raw()
        c2.use_item(CHEST_X, CHEST_Y, CHEST_Z, 1, item_id=CHEST_BLOCK)
        deadline = time.time() + 3.0
        while time.time() < deadline:
            c2.drain(0.25)
            pids = [pid for pid, _ in c2.raw]
            if CONTAINER_OPEN in pids and CONTAINER_SET_CONTENT in pids:
                break
        seq = c2.raw
        seq_pids = [pid for pid, _ in seq]
        open_idx = seq_pids.index(CONTAINER_OPEN) if CONTAINER_OPEN in seq_pids else -1
        check(open_idx >= 0, "chest ContainerOpen sent")
        be_before = [1 for pid, _ in seq[:open_idx] if pid == BLOCK_ENTITY_DATA]
        check(bool(be_before),
              "BlockEntityData arrives BEFORE the chest ContainerOpen")
        if open_idx >= 0:
            op = seq[open_idx][1]
            check(len(op) == 17,
                  f"chest ContainerOpen is 17 bytes total (got {len(op)})")
            typ = op[2]
            slots = struct.unpack(">h", op[3:5])[0]
            check(typ == 0 and slots == 27,
                  f"chest open type=0 slots=27 (got type={typ} slots={slots})")

        console("stop")
    finally:
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
        except (BrokenPipeError, OSError):
            pass
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
        if not args.keep:
            import shutil
            shutil.rmtree(runtime, ignore_errors=True)

    print()
    if failures:
        print(f"FAILED: {len(failures)} check(s):")
        for f in failures:
            print("  - " + f)
        return 1
    print("ALL PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
