#!/usr/bin/env python3
"""End-to-end chest co-viewer sync check for CurseStone.

The multiplayer chest bug: with two players holding the same chest open,
a slot change made by one was never shown to the other (no ContainerSetSlot
was ever sent to the co-viewer). The stale second viewer then clicked what
it still believed was an empty slot and silently overwrote the first
viewer's item — the server parked the displaced stack in that viewer's
cursor buffer, so the placed item "came back" through the inventory when
the container closed.

Two real protocol-84 clients (MCPE 0.15.10) share one chest, both open:

  1. A writes slot 5 (apple x3)  -> B receives ContainerSetSlot on B's own
     chest window with the same content (the co-viewer broadcast)
  2. B writes slot 7 (bread x2)  -> A receives it on A's window
  3. A clears slot 5             -> B sees the slot go empty
  4. both close and A reopens    -> the content dump still holds BOTH
     writes (apple x3 in 5, bread x2 in 7): nothing rolled back

    python3 tools/chest_cowiewer_sync_check.py [--server /path/to/CurseStone]

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
from chest_milk_check import MilkClient  # noqa: E402
from door_bed_flow_check import PLAYER_ACTION_SPAWN_SAME_DIMENSION  # noqa: E402

CONTAINER_SET_SLOT = 0x2C
CONTAINER_SET_CONTENT = 0x2E
CONTAINER_OPEN = 0x2A

CHEST_BLOCK = 54
APPLE = 260
BREAD = 297

CHEST_X, CHEST_Z = 134, 134


def b64u(obj) -> str:
    raw = json.dumps(obj, separators=(",", ":")).encode()
    return base64.urlsafe_b64encode(raw).rstrip(b"=").decode("ascii")


class SyncClient(MilkClient):
    """MilkClient + server->client ContainerSetSlot capture (the co-viewer
    broadcast), chest-window content dumps and the creative chest-slot
    write the click flow needs."""

    def __init__(self, host, port, name, verbose=False):
        super().__init__(host, port, name, verbose)
        self.slot_pkts = []     # (windowid, slot, id, count, meta)
        self.chest_dumps = {}   # windowid -> {slot: (id, count, meta)}

    def on_game_packet(self, p: bytes):
        if not p:
            return
        pid = p[0]
        if pid == CONTAINER_SET_SLOT and len(p) >= 8:
            win = p[1]
            slot, _hot = struct.unpack(">hh", p[2:6])
            sid = struct.unpack(">h", p[6:8])[0]
            cnt, meta = 0, 0
            if sid > 0 and len(p) >= 11:
                cnt = p[8]
                meta = struct.unpack(">h", p[9:11])[0]
            self.slot_pkts.append((win, slot, sid, cnt, meta))
            return
        if pid == CONTAINER_SET_CONTENT and len(p) >= 4:
            win = p[1]
            count = struct.unpack(">H", p[2:4])[0]
            off = 4
            slots = {}
            for idx in range(count):
                if off + 2 > len(p):
                    break
                sid = struct.unpack(">h", p[off:off + 2])[0]
                off += 2
                if sid == 0:
                    continue
                if off + 5 > len(p):
                    break
                cnt = p[off]
                meta = struct.unpack(">h", p[off + 1:off + 3])[0]
                nlen = struct.unpack("<H", p[off + 3:off + 5])[0]
                off += 5 + nlen
                slots[idx] = (sid, cnt, meta)
            self.chest_dumps[win] = slots
            # window-0 dumps still feed MilkClient's inventory tracking
            if win != 0:
                return
        SyncClientMilk.on_game_packet(self, p)

    def chest_slot(self, window, slot, item_id, count=1, meta=0):
        cs = bytes([CONTAINER_SET_SLOT, window & 0xFF])
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", slot)
        cs += struct.pack(">h", item_id)
        cs += bytes([count])
        cs += struct.pack(">h", meta)
        cs += struct.pack("<H", 0)
        self.send_game(cs)


# Alias so the delegation above reaches MilkClient's handlers (the class
# body cannot reference MilkClient before it exists).
SyncClientMilk = MilkClient


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone chest co-viewer sync check")
    ap.add_argument("--server", default="/tmp/cs_build/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19149)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_chestsync_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=ChestSync\nview-distance=4\ngamemode=0\n"
                "advanced-log=true\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("SyncOne\nSyncTwo\n")
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
        deadline = time.time() + 90
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

        a = SyncClient(args.host, args.port, "SyncOne", verbose=args.verbose)
        print("stage: A handshake+login+spawn")
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

        # Creative for both: the chest slot writes must apply directly
        # (the survival floating-buffer path is chest_milk_check's turf).
        a.say("/gamemode 1")
        time.sleep(0.8)
        a.drain(0.8)
        a.mob_equipment(3, CHEST_BLOCK, 0)
        time.sleep(0.2)
        a.creative_take(3, CHEST_BLOCK, 1, 0)
        time.sleep(0.4)
        a.drain(0.4)

        # A places the chest block.
        a.use_item(CHEST_X, 3, CHEST_Z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        ups = [e for e in a.events if e[0] == "update_block"
               and e[1] == CHEST_X and e[2] == 4 and e[3] == CHEST_Z]
        check(bool(ups) and ups[0][4] == CHEST_BLOCK,
              "chest block places (UpdateBlock id 54)", f"ups={a.events}")

        # B joins and shares the chunk.
        b = SyncClient(args.host, args.port, "SyncTwo", verbose=args.verbose)
        print("stage: B handshake+login+spawn")
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
            print("FAIL: B never received StartGame (no eid)")
            return 1
        print(f"  B eid={b.eid}")
        b.say("/gamemode 1")
        time.sleep(0.8)
        b.drain(0.8)

        # Both open the SAME chest — each with its own window id.
        a.container_opens.clear()
        a.use_item(CHEST_X, 4, CHEST_Z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        win_a = a.container_opens[-1][0] if a.container_opens else 0
        check(bool(a.container_opens) and
              a.container_opens[-1][1:] == (0, CHEST_X, 4, CHEST_Z),
              "A opens the chest", f"opens={a.container_opens}")

        b.container_opens.clear()
        b.use_item(CHEST_X, 4, CHEST_Z, 1)
        b.drain(1.2)
        win_b = b.container_opens[-1][0] if b.container_opens else 0
        check(bool(b.container_opens) and
              b.container_opens[-1][1:] == (0, CHEST_X, 4, CHEST_Z),
              "B opens the same chest", f"opens={b.container_opens}")
        print(f"  win_a={win_a} win_b={win_b}")

        # -------- 1: A writes slot 5 -> B sees it on B's own window ------
        mark_a = len(a.slot_pkts)
        mark_b = len(b.slot_pkts)
        a.chest_slot(win_a, 5, APPLE, 3)
        a.drain(1.0)
        b.drain(1.0)
        a_confirm = [p for p in a.slot_pkts[mark_a:]
                     if p[0] == win_a and p[1] == 5]
        check(bool(a_confirm) and a_confirm[0][2] == APPLE and a_confirm[0][3] == 3,
              "A's own write is confirmed back on A's window",
              f"a.slot_pkts={a.slot_pkts[mark_a:]}")
        b_seen = [p for p in b.slot_pkts[mark_b:]
                  if p[0] == win_b and p[1] == 5]
        check(bool(b_seen) and b_seen[0][2] == APPLE and b_seen[0][3] == 3,
              "B receives the co-viewer broadcast for slot 5 (apple x3)",
              f"b.slot_pkts={b.slot_pkts[mark_b:]}")

        # -------- 2: B writes slot 7 -> A sees it -------------------------
        mark_a = len(a.slot_pkts)
        mark_b = len(b.slot_pkts)
        b.chest_slot(win_b, 7, BREAD, 2)
        a.drain(1.0)
        b.drain(1.0)
        a_seen = [p for p in a.slot_pkts[mark_a:]
                  if p[0] == win_a and p[1] == 7]
        check(bool(a_seen) and a_seen[0][2] == BREAD and a_seen[0][3] == 2,
              "A receives the co-viewer broadcast for slot 7 (bread x2)",
              f"a.slot_pkts={a.slot_pkts[mark_a:]}")
        b_confirm = [p for p in b.slot_pkts[mark_b:]
                     if p[0] == win_b and p[1] == 7]
        check(bool(b_confirm) and b_confirm[0][2] == BREAD and b_confirm[0][3] == 2,
              "B's own write is confirmed back on B's window",
              f"b.slot_pkts={b.slot_pkts[mark_b:]}")

        # -------- 3: A clears slot 5 -> B sees it go empty ----------------
        mark_b = len(b.slot_pkts)
        a.chest_slot(win_a, 5, 0, 0)
        a.drain(1.0)
        b.drain(1.0)
        b_empty = [p for p in b.slot_pkts[mark_b:]
                   if p[0] == win_b and p[1] == 5]
        check(bool(b_empty) and b_empty[0][2] == 0,
              "B sees slot 5 go empty after A takes the stack out",
              f"b.slot_pkts={b.slot_pkts[mark_b:]}")

        # -------- 4: nothing rolls back across close + reopen -------------
        a.chest_slot(win_a, 5, APPLE, 3)
        time.sleep(0.4)
        a.drain(0.4)
        b.drain(0.4)
        a.container_close(win_a)
        time.sleep(0.5)
        b.container_close(win_b)
        time.sleep(0.5)
        a.drain(0.8)
        b.drain(0.8)

        a.container_opens.clear()
        a.chest_dumps.clear()
        a.use_item(CHEST_X, 4, CHEST_Z, 1, item_id=CHEST_BLOCK)
        a.drain(1.2)
        win_a2 = a.container_opens[-1][0] if a.container_opens else 0
        dump = a.chest_dumps.get(win_a2, {})
        check(dump.get(5, (0, 0, 0))[:2] == (APPLE, 3),
              "reopened chest still holds A's apple x3 in slot 5",
              f"dump={dump}")
        check(dump.get(7, (0, 0, 0))[:2] == (BREAD, 2),
              "reopened chest still holds B's bread x2 in slot 7 "
              "(no rollback of the co-viewer's write)",
              f"dump={dump}")

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
