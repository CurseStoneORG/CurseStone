#!/usr/bin/env python3
"""Held-item container-open check for CurseStone (protocol 84 / 0.15.10).

Reproduces the "furnace only opens with an empty hand" report: a real
0.15.10 touch client taps a furnace while HOLDING an item. The server must
run block activation (ContainerOpen) BEFORE any item-use shortcut, so the
held stack never swallows the tap:

  1. bread (food) in hand        -> tap furnace  -> ContainerOpen type=2
  2. raw porkchop (food) in hand -> tap furnace  -> ContainerOpen type=2
  3. cobblestone (block) in hand -> tap furnace  -> ContainerOpen type=2
  4. splash potion in hand       -> tap furnace  -> ContainerOpen type=2
                                    (activation beats the throw, vanilla
                                    onBlockActivated-first ordering)
  5. empty hand                  -> tap furnace  -> ContainerOpen type=2
                                    (regression guard for the old path)
  6. bread in hand               -> tap chest    -> ContainerOpen type=0
                                    (the same fix must cover every
                                    interactive block, not just furnaces)
  7. bread in hand               -> tap stone    -> NO ContainerOpen
                                    (negative control: plain blocks must
                                    not open anything)
  8. the furnace open is USABLE: the 3-slot ContainerSetContent dump for
     the window follows the open record.

    python3 tools/furnace_held_item_open_check.py [--server /path/to/CurseStone]

Exit code 0 = all expected wire facts hold; 1 = anything deviates.
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

CONTAINER_OPEN = 0x2A
CONTAINER_SET_CONTENT = 0x2E

FURNACE = 61
CHEST_BLOCK = 54
STONE = 1
BREAD = 297
RAW_PORKCHOP = 319
COBBLE = 4
SPLASH_POTION = 438

FURN_POS = (130, 4, 131)
CHEST_POS = (136, 4, 136)
STONE_POS = (140, 4, 140)
HOME = (128, 6, 128)

PLUGIN_DIR = "plugins/HeldOpen"
PLUGIN_YML = ("name: HeldOpen\nmain: src/main.lua\napi: 1.0.2\n"
              "version: 1.0.0\nauthor: CurseStone\n")
PLUGIN_LUA = '''local plugin = {}
local joined = false

local function stage()
    local w = server.defaultWorld()
    w:setBlock(130, 4, 131, 61, 2)  -- furnace
    w:setBlock(136, 4, 136, 54, 2)  -- chest
    w:setBlock(140, 4, 140, 1, 0)   -- stone (negative control)
    log.info("HELDOPEN staged furnace/chest/stone")
end

function plugin.onEnable()
    -- Stage on the FIRST PlayerJoin: World::setBlock forces the chunk to
    -- load, so all three blocks land BEFORE the client streams the chunk.
    events.on("PlayerJoin", function(e)
        if joined then return end
        joined = true
        stage()
    end)
    commands.register("heldopen", {description = "stage furnace/chest/stone"},
                     function(args, sender)
        stage()
    end)
    log.info("held-open plugin ready")
    return true
end

function plugin.onDisable()
end

return plugin
'''


class WireClient(SyncClient):
    """Also captures ContainerSetContent dumps per window id."""

    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        self.content_dumps = {}  # windowid -> slot count

    def on_game_packet(self, p: bytes):
        if p and p[0] == CONTAINER_SET_CONTENT and len(p) >= 4:
            win = p[1]
            n = struct.unpack(">h", p[2:4])[0]
            self.content_dumps[win] = n
        super().on_game_packet(p)


def main() -> int:
    ap = argparse.ArgumentParser(description="CurseStone held-item container open check")
    ap.add_argument("--server", default="/home/z/my-project/build/cs-rel/CurseStone")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=19177)
    ap.add_argument("--keep", action="store_true")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    if not os.path.isfile(args.server):
        print(f"FAIL: server binary not found: {args.server}")
        return 1

    runtime = tempfile.mkdtemp(prefix="cs_heldopen_")
    with open(os.path.join(runtime, "server.properties"), "w") as f:
        f.write("server-ip=127.0.0.1\nserver-port=%d\nmax-players=5\n"
                "motd=HeldOpen\nview-distance=4\ngamemode=1\n" % args.port)
    with open(os.path.join(runtime, "ops.txt"), "w") as f:
        f.write("HeldBot\n")
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

        c = WireClient(args.host, args.port, "HeldBot", verbose=args.verbose)
        print("stage: bot login+spawn")
        if not c.handshake() or not c.login():
            print("FAIL: handshake/login")
            return 1
        c.drain(4.0)
        c.player_action(PLAYER_ACTION_SPAWN_SAME_DIMENSION)
        c.drain(1.5)

        logtxt = open(server_log).read()
        check("HELDOPEN staged" in logtxt,
              "plugin staged furnace/chest/stone blocks",
              [ln for ln in logtxt.splitlines() if "HELDOPEN" in ln][-1:])

        # Hotbar slots: one distinct inventory slot per scenario so the
        # server-side held stack is exactly what the test wants (creative_take
        # writes the slot, mob_equipment selects it — same pairing the other
        # wire checks use).
        next_slot = [0]

        def hold(item_id, meta=0):
            slot = next_slot[0]
            next_slot[0] += 1
            if item_id > 0:
                c.creative_take(slot, item_id, 1, meta)
            c.mob_equipment(slot, item_id, meta)
            time.sleep(0.15)
            c.drain(0.15)

        def tap(pos, face, item_id, expect_open, expect_type=None, label=""):
            """Tap `pos` with `item_id` held; assert the ContainerOpen fact."""
            c.container_opens.clear()
            c.use_item(pos[0], pos[1], pos[2], face, item_id=item_id)
            c.drain(1.2)
            opens = [o for o in c.container_opens if o[2:] == pos]
            ok = bool(opens) == expect_open
            if ok and expect_open and expect_type is not None:
                ok = opens[0][1] == expect_type and opens[0][0] >= 2
            check(ok, label,
                  f"opens={c.container_opens}")
            return opens

        # -- 1..4: the furnace must open with ANY held item ---------------
        hold(BREAD)
        tap(FURN_POS, 1, BREAD, True, 2,
            "furnace opens with BREAD in hand (the reported bug)")

        hold(RAW_PORKCHOP)
        tap(FURN_POS, 1, RAW_PORKCHOP, True, 2,
            "furnace opens with RAW PORKCHOP in hand")

        hold(COBBLE)
        tap(FURN_POS, 1, COBBLE, True, 2,
            "furnace opens with COBBLESTONE in hand")

        hold(SPLASH_POTION)
        tap(FURN_POS, 1, SPLASH_POTION, True, 2,
            "furnace opens with SPLASH POTION in hand (activation beats throw)")

        # -- 5: empty hand regression guard -------------------------------
        hold(0)
        tap(FURN_POS, 1, 0, True, 2,
            "furnace still opens with an EMPTY hand")

        # -- 6: the chest gets the same treatment -------------------------
        hold(BREAD)
        tap(CHEST_POS, 1, BREAD, True, 0,
            "chest opens with BREAD in hand (same class of bug)")

        # -- 7: negative control — plain blocks never open ----------------
        tap(STONE_POS, 1, BREAD, False,
            label="stone does NOT open a container (negative control)")

        # -- 8: the open furnace window is usable -------------------------
        c.container_opens.clear()
        c.content_dumps.clear()
        c.use_item(FURN_POS[0], FURN_POS[1], FURN_POS[2], 1, item_id=BREAD)
        c.drain(1.2)
        opens = [o for o in c.container_opens if o[2:] == FURN_POS]
        if opens:
            win = opens[0][0]
            check(c.content_dumps.get(win) == 3,
                  "furnace window gets the 3-slot content dump",
                  f"dumps={c.content_dumps}")
            c.container_close(win)
            c.drain(0.5)
        else:
            check(False, "furnace window gets the 3-slot content dump",
                  f"opens={c.container_opens}")

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
