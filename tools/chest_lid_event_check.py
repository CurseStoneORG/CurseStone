#!/usr/bin/env python3
"""End-to-end chest lid animation (BlockEventPacket) wire check for CurseStone.

The user-reported bug: tapping a chest opened the container UI but the
lid never animated and no open/close sound played. The lid animation
and the sounds on a 0.15.10 client are NOT driven by ContainerOpen —
they are driven ONLY by a BlockEventPacket addressed to the chest's
block coordinates, with the payload the era reference servers
(the 0.15-era server software — the software that drove real
0.15.10 clients) put on the wire:

  case1 = 1   ALWAYS — the chest-lid event id ("it's always 1 for a
              chest", the era reference ChestInventory)
  case2 = 2   lid UP   (open animation + open sound)
  case2 = 0   lid DOWN (close animation + close sound)

The old build sent case1=1/case2=0 on open (which the client parses as
"zero viewers" — lid stays shut, no sound) and case1=0/case2=0 on close
(a non-chest event the client drops). This check pins the corrected
wire payload so the lid can never silently regress again:

  1. place a chest, tap it and capture the open batch:
       - ContainerOpen (type 0, 27 slots, chest coords)
       - ContainerSetContent (27 slots)
       - BlockEventPacket at the chest coords with (case1, case2) = (1, 2),
         arriving AFTER the ContainerOpen (era order: window first, lid
         event last)
  2. close the window from the client: a BlockEventPacket with
     (1, 0) at the chest coords must follow
  3. tap again: the lid-open event (1, 2) must fire again — proves the
     viewer count is not leaking through the close path (a leak would
     skip the open event on every open after the first)
  4. open a chest and die with it open (/kill), respawn and tap: the
     open event must still fire — death closes the container like
     vanilla (a leaked viewer from death would suppress it)
  5. place a second chest adjacent (vanilla auto-pair), tap one half:
     BOTH halves must get their own (1, 2) lid event and the window
     must be the 54-slot double window

    python3 tools/chest_lid_event_check.py [--server /path/to/CurseStone]

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

# Protocol-84 packet ids (the 1.6-era reference implementation Info.php) — NOT the later
# 0.16/1.x renumbering.
BLOCK_EVENT = 0x17
CONTAINER_OPEN = 0x2A
CONTAINER_CLOSE = 0x2B
CONTAINER_SET_CONTENT = 0x2E

CHEST_X, CHEST_Y, CHEST_Z = -4, 4, -2
DOUBLE_X, DOUBLE_Y, DOUBLE_Z = -5, 4, -2  # second half, adjacent in X
TP_X, TP_Y, TP_Z = -8, 6, -8
CHEST_BLOCK = 54


class LidClient(SyncClient):
    """SyncClient + raw capture of the lid-relevant packets."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.raw = []  # (pid, bytes)

    def on_game_packet(self, p: bytes):
        if p and p[0] in (BLOCK_EVENT, CONTAINER_OPEN, CONTAINER_CLOSE,
                          CONTAINER_SET_CONTENT):
            self.raw.append((p[0], bytes(p)))
            return
        super().on_game_packet(p)

    def clear_raw(self):
        self.raw = []


def block_events(raw):
    """Decodes captured BlockEventPackets: [(x, y, z, case1, case2)]."""
    out = []
    for pid, p in raw:
        if pid == BLOCK_EVENT and len(p) == 21:
            x, y, z, c1, c2 = struct.unpack(">iiiii", p[1:21])
            out.append((x, y, z, c1, c2))
    return out


def opens(raw):
    """Decodes captured ContainerOpenPackets: [(windowid, type, slots, x, y, z)]."""
    out = []
    for pid, p in raw:
        if pid == CONTAINER_OPEN and len(p) == 17:
            typ = p[2]
            slots = struct.unpack(">h", p[3:5])[0]
            x, y, z = struct.unpack(">iii", p[5:17])
            out.append((p[1], typ, slots, x, y, z))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone chest lid event wire check")
    ap.add_argument("--server", default="/home/z/my-project/work/build-git/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19153)
    ap.add_argument("--name", default="LidTester")
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    runtime = tempfile.mkdtemp(prefix="cs_lidevent_")
    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=LidEvent\nview-distance=8\ngamemode=1\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write(args.name + "\n")
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

    def wait_for_opens(c, timeout=4.0):
        deadline = time.time() + timeout
        while time.time() < deadline:
            c.drain(0.25)
            if opens(c.raw):
                return True
        return False

    def place_chest(c, x, y, z):
        c.mob_equipment(2, CHEST_BLOCK, 0)
        time.sleep(0.2)
        c.creative_take(2, CHEST_BLOCK, 1, 0)
        time.sleep(0.2)
        c.drain(0.2)
        c.use_item(x, y - 1, z, 1, item_id=CHEST_BLOCK)
        time.sleep(0.8)
        c.drain(0.4)

    try:
        if not boot_and_wait():
            print("FAIL: server never became ready")
            return 1

        c = LidClient(args.host, args.port, args.name, verbose=args.verbose)
        print("stage: handshake+login + chest placement")
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)
        if c.eid == 0:
            print("FAIL: never received StartGame (no eid)")
            return 1

        c.say("/gamemode 1")
        time.sleep(0.8)
        c.drain(0.8)
        c.say(f"/tp {TP_X} {TP_Y} {TP_Z}")
        time.sleep(1.0)
        c.drain(1.0)
        place_chest(c, CHEST_X, CHEST_Y, CHEST_Z)
        ups = [e for e in c.events if e[0] == "update_block"]
        chest_ups = [e for e in ups
                     if e[1] == CHEST_X and e[2] == CHEST_Y and e[3] == CHEST_Z]
        check(bool(chest_ups) and chest_ups[-1][4] == CHEST_BLOCK,
              "chest block placed")

        # -------- 1. tap the chest: the lid-open event ------------------
        print("stage: chest tap open batch")
        c.clear_raw()
        c.use_item(CHEST_X, CHEST_Y, CHEST_Z, 1, item_id=CHEST_BLOCK)
        wait_for_opens(c)
        seq = c.raw
        seq_pids = [pid for pid, _ in seq]

        open_idx = seq_pids.index(CONTAINER_OPEN) if CONTAINER_OPEN in seq_pids else -1
        check(open_idx >= 0, "chest ContainerOpen sent")
        win = 0
        if open_idx >= 0:
            win, typ, slots, ox, oy, oz = opens(seq)[0]
            check(typ == 0 and slots == 27,
                  f"open type=0 slots=27 (got type={typ} slots={slots})")
            check((ox, oy, oz) == (CHEST_X, CHEST_Y, CHEST_Z),
                  f"open coords match (got {ox},{oy},{oz})")
            check(win >= 2, f"open windowid>=2 (got {win})")

        evs = block_events(seq)
        check((CHEST_X, CHEST_Y, CHEST_Z, 1, 2) in evs,
              f"lid-open BlockEventPacket (1, 2) at the chest coords "
              f"(got {evs})")
        ev_idx = seq_pids.index(BLOCK_EVENT) if BLOCK_EVENT in seq_pids else -1
        check(open_idx >= 0 and ev_idx > open_idx,
              "lid-open event arrives AFTER the ContainerOpen (era order)")
        ci = seq_pids.index(CONTAINER_SET_CONTENT) if CONTAINER_SET_CONTENT in seq_pids else -1
        check(ci >= 0, "ContainerSetContent content dump sent in the open batch")

        # -------- 2. client closes the window: the lid-close event ------
        print("stage: client window close")
        c.clear_raw()
        c.container_close(win)
        deadline = time.time() + 4.0
        while time.time() < deadline:
            c.drain(0.25)
            if BLOCK_EVENT in [pid for pid, _ in c.raw]:
                break
        evs = block_events(c.raw)
        check((CHEST_X, CHEST_Y, CHEST_Z, 1, 0) in evs,
              f"lid-close BlockEventPacket (1, 0) after the window close "
              f"(got {evs})")
        check((CHEST_X, CHEST_Y, CHEST_Z, 1, 2) not in evs,
              "close path sends no stray lid-open event")

        # -------- 3. reopen: the open event must fire again --------------
        print("stage: reopen after close")
        c.clear_raw()
        c.use_item(CHEST_X, CHEST_Y, CHEST_Z, 1, item_id=CHEST_BLOCK)
        wait_for_opens(c)
        evs = block_events(c.raw)
        check((CHEST_X, CHEST_Y, CHEST_Z, 1, 2) in evs,
              f"lid-open event fires again on reopen (got {evs})")
        if opens(c.raw):
            win2 = opens(c.raw)[0][0]
            c.container_close(win2)
            time.sleep(0.8)
            c.drain(0.6)

        # -------- 4. death with the chest open ---------------------------
        print("stage: die with the chest open, respawn, tap again")
        c.clear_raw()
        c.use_item(CHEST_X, CHEST_Y, CHEST_Z, 1, item_id=CHEST_BLOCK)
        wait_for_opens(c)
        pre_open = opens(c.raw)
        check(bool(pre_open), "chest open before death")
        c.say("/gamemode 0")
        time.sleep(0.6)
        c.drain(0.4)
        c.say(f"/kill {args.name}")
        time.sleep(2.5)
        c.drain(0.5)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        time.sleep(3.0)
        c.drain(1.0)
        c.say("/gamemode 1")
        time.sleep(0.8)
        c.drain(0.6)
        c.say(f"/tp {TP_X} {TP_Y} {TP_Z}")
        time.sleep(1.0)
        c.drain(1.0)
        c.clear_raw()
        # A few taps for timing robustness: the first may race the
        # respawn snap, later ones are no-ops once the window is open.
        for _ in range(3):
            c.use_item(CHEST_X, CHEST_Y, CHEST_Z, 1, item_id=CHEST_BLOCK)
            if wait_for_opens(c, timeout=2.5):
                break
        evs = block_events(c.raw)
        check((CHEST_X, CHEST_Y, CHEST_Z, 1, 2) in evs,
              f"lid-open event fires after death+respawn (death released "
              f"the viewer) (got {evs})")
        if opens(c.raw):
            win3 = opens(c.raw)[0][0]
            c.container_close(win3)
            time.sleep(0.8)
            c.drain(0.6)

        # -------- 5. double chest: both halves animate -------------------
        print("stage: double chest pairing")
        place_chest(c, DOUBLE_X, DOUBLE_Y, DOUBLE_Z)
        c.clear_raw()
        c.use_item(DOUBLE_X, DOUBLE_Y, DOUBLE_Z, 1, item_id=CHEST_BLOCK)
        wait_for_opens(c)
        evs = block_events(c.raw)
        check((DOUBLE_X, DOUBLE_Y, DOUBLE_Z, 1, 2) in evs,
              f"lid-open event at the tapped half (got {evs})")
        check((CHEST_X, CHEST_Y, CHEST_Z, 1, 2) in evs,
              f"lid-open event at the paired half too (got {evs})")
        dops = opens(c.raw)
        check(bool(dops) and dops[0][2] == 54,
              f"double window opens with 54 slots "
              f"(got {[(t[1], t[2]) for t in dops]})")
        if dops:
            c.container_close(dops[0][0])
            time.sleep(0.8)
            c.drain(0.6)

        print()
        print("stopping server")
        proc.stdin.write(b"stop\n")
        proc.stdin.flush()
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.kill()
    except Exception:
        import traceback
        traceback.print_exc()
        failures.append("unexpected exception")
        try:
            proc.stdin.write(b"stop\n")
            proc.stdin.flush()
            proc.wait(timeout=10)
        except (BrokenPipeError, OSError, subprocess.TimeoutExpired):
            proc.kill()
    finally:
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
